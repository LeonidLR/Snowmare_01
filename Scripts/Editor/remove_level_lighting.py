"""Removes the level's own global lighting actors so the weather presenter's own rig lights the map (docs/systems/environment-weather.md
section 16, game designer 2026-10-25: "If I just need to delete the lighting folder from all levels and keep only our Blueprint, tell me").

AWeatherPresenterActor builds its own sun / moon / sky light / sky atmosphere / height fog / post-process when a level has none, and keeps
ADOPTING a level's own actors while they exist (backward compatible). This script removes exactly the actors the presenter replaces:
  - DirectionalLight (the sun and an optional moon),
  - SkyLight, SkyAtmosphere, ExponentialHeightFog,
  - VolumetricCloud (switched off by the presenter anyway: the tactical camera never sees it, user decision 2026-10-22),
  - an UNBOUND PostProcessVolume (global grade; bounded volumes are local looks and stay),
  - the template's sky dome (StaticMeshActor with /Engine/EngineSky/SM_SkySphere; the presenter's sky atmosphere draws the sky).
Everything else stays - also other actors in a "Lighting" outliner folder (point / spot / rect lights, reflection captures, lightmass
volumes ...): they are LISTED as "kept" so the designer can decide. Then ONE weather presenter is placed (label WeatherPresenter, folder
Lighting; BP_WeatherPresenter when it exists, else AWeatherPresenterActor): in the editor it lights the viewport as its PreviewHour /
PreviewWeather (no lights left otherwise), in game it is the presenter (found instead of spawned). CODEX_PLACE_PRESENTER=0 skips it.

Modes (environment variables):
  default            DRY RUN: loads each map, lists every actor that would be removed / kept, writes nothing.
  CODEX_APPLY=1      backs every map file up first (Saved/Backups/remove_level_lighting_<stamp>/, SHA1 manifest, copy verified), then
                     removes the actors and saves the map.
  CODEX_SAVE_AS=/Game/Maps/_Test/L_LakeNoLights
                     test mode (one map in CODEX_MAPS): duplicates the map to that NEW package, removes the actors there and saves only the
                     copy; the source map is never saved. Do not commit the copy.
  CODEX_MAPS=/Game/Maps/A,/Game/Maps/B
                     the maps (default: the two Demo 01 levels below).

Navmesh: the lake's A4 ambush smokes need the EDITOR-built navmesh saved in the map (docs/port/TRAPS.md). A commandlet (-run=pythonscript)
that loads and saves a map drops it, so with a RecastNavMesh in the map CODEX_APPLY refuses to save from a commandlet unless
CODEX_ALLOW_COMMANDLET=1 (then bake it afterwards: Scripts/bake_navmesh.ps1 -Map <name>). Run it in the full editor instead (it quits after
the script; this checkout's editor otherwise closed):
  UnrealEditor.exe <uproject> -ExecutePythonScript="<abs path to this file>" -unattended -nosplash      (with CODEX_APPLY=1 set)
Dry run / test copy also work as a commandlet:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Report: Saved/Logs/remove_level_lighting.txt
"""
import datetime
import hashlib
import os
import shutil

import unreal

DEFAULT_MAPS = ["/Game/Maps/L_Demo01_A_FrozenLake", "/Game/Maps/L_Demo01_B_FrozenFactory"]
# Classes the presenter replaces (exact class names; a Blueprint child of one of them counts too).
REPLACED = ["DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud", "PostProcessVolume"]
LIGHTING_FOLDER_WORDS = ("light", "sky", "atmos", "fog")
PRESENTER_BP = "/Game/Environment/Weather/BP_WeatherPresenter"
# The template's sky dome (StaticMeshActor "SM_SkySphere", material M_SimpleSkyDome): removed with the sky atmosphere it shows.
SKY_DOME_MESH = "/Engine/EngineSky/SM_SkySphere"

SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
CONTENT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
REPORT = []


def report(line=""):
    REPORT.append(str(line))


def flush():
    os.makedirs(os.path.join(SAVED, "Logs"), exist_ok=True)
    with open(os.path.join(SAVED, "Logs", "remove_level_lighting.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def package_path(name):
    """A map name ("L_X"), a /Game/... path, or a /Game/... path mangled by a POSIX shell (".../Git/Game/Maps/L_X") -> /Game/Maps/L_X."""
    name = name.strip().replace("\\", "/")
    if "/Game/" in name:
        return "/Game/" + name.split("/Game/", 1)[1]
    return "/Game/Maps/" + name


def map_file(package):
    """/Game/Maps/X -> <Content>/Maps/X.umap"""
    return os.path.normpath(os.path.join(CONTENT, package[len("/Game/"):] + ".umap"))


def class_chain(actor):
    """Names of the actor's class and its native parents (a BP child of DirectionalLight -> [..., 'DirectionalLight', 'Light', 'Info', 'Actor'])."""
    names = []
    cls = actor.get_class()
    while cls:
        names.append(cls.get_name())
        try:
            cls = cls.get_super_class()
        except Exception:
            break
    return names


def replaced_class(actor):
    chain = class_chain(actor)
    for name in REPLACED:
        if name in chain:
            return name
    return None


def safe(fn, default="?"):
    try:
        return fn()
    except Exception:
        return default


def describe(actor, kind):
    """Key values of a lighting actor (also the reference for the presenter's own-rig defaults)."""
    loc = actor.get_actor_location()
    rot = actor.get_actor_rotation()
    head = "%-34s %-22s folder '%s' at (%.0f, %.0f, %.0f) rot (p %.1f, y %.1f)" % (
        actor.get_actor_label(), actor.get_class().get_name(), str(actor.get_folder_path()), loc.x, loc.y, loc.z, rot.pitch, rot.yaw)
    extra = ""
    if kind == "DirectionalLight":
        c = actor.get_component_by_class(unreal.DirectionalLightComponent)
        if c:
            extra = "intensity %s lux, colour %s, temperature %s (use %s), mobility %s, atmosphere sun %s index %s, source angle %s, shadows %s, dyn shadow dist %s, cascades %s" % (
                safe(lambda: c.get_editor_property("intensity")), safe(lambda: c.get_editor_property("light_color")),
                safe(lambda: c.get_editor_property("temperature")), safe(lambda: c.get_editor_property("use_temperature")),
                safe(lambda: c.get_editor_property("mobility")), safe(lambda: c.get_editor_property("atmosphere_sun_light")),
                safe(lambda: c.get_editor_property("atmosphere_sun_light_index")), safe(lambda: c.get_editor_property("light_source_angle")),
                safe(lambda: c.get_editor_property("cast_shadows")), safe(lambda: c.get_editor_property("dynamic_shadow_distance_movable_light")),
                safe(lambda: c.get_editor_property("dynamic_shadow_cascades")))
    elif kind == "SkyLight":
        c = actor.get_component_by_class(unreal.SkyLightComponent)
        if c:
            extra = "intensity %s, real-time capture %s, mobility %s, source %s, lower hemisphere black %s, colour %s" % (
                safe(lambda: c.get_editor_property("intensity")), safe(lambda: c.get_editor_property("real_time_capture")),
                safe(lambda: c.get_editor_property("mobility")), safe(lambda: c.get_editor_property("source_type")),
                safe(lambda: c.get_editor_property("lower_hemisphere_is_black")), safe(lambda: c.get_editor_property("light_color")))
    elif kind == "SkyAtmosphere":
        c = actor.get_component_by_class(unreal.SkyAtmosphereComponent)
        if c:
            extra = "rayleigh %s, mie %s, transform mode %s" % (safe(lambda: c.get_editor_property("rayleigh_scattering_scale")),
                safe(lambda: c.get_editor_property("mie_scattering_scale")), safe(lambda: c.get_editor_property("transform_mode")))
    elif kind == "ExponentialHeightFog":
        c = actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
        if c:
            extra = "density %s, falloff %s, inscattering %s, start %s, max opacity %s, volumetric %s" % (
                safe(lambda: c.get_editor_property("fog_density")), safe(lambda: c.get_editor_property("fog_height_falloff")),
                safe(lambda: c.get_editor_property("fog_inscattering_luminance")), safe(lambda: c.get_editor_property("start_distance")),
                safe(lambda: c.get_editor_property("fog_max_opacity")), safe(lambda: c.get_editor_property("enable_volumetric_fog")))
    elif kind == "StaticMeshActor" or kind == "SkySphere":
        c = actor.get_component_by_class(unreal.StaticMeshComponent)
        if c:
            mesh = c.get_editor_property("static_mesh")
            mats = [m.get_path_name() if m else "none" for m in safe(lambda: c.get_materials(), [])]
            extra = "mesh %s, materials %s, hidden in game %s" % (mesh.get_path_name() if mesh else "none", mats,
                safe(lambda: actor.get_editor_property("hidden")))
    elif kind == "PostProcessVolume":
        extra = "unbound %s, enabled %s, priority %s" % (safe(lambda: actor.get_editor_property("unbound")),
            safe(lambda: actor.get_editor_property("enabled")), safe(lambda: actor.get_editor_property("priority")))
    return head + (" | " + extra if extra else "")


def in_lighting_folder(actor):
    folder = str(actor.get_folder_path()).lower()
    return any(word in folder for word in LIGHTING_FOLDER_WORDS)


def is_sky_dome(actor):
    """The template's sky sphere (StaticMeshActor with /Engine/EngineSky/SM_SkySphere): it only shows the sky atmosphere, which the presenter draws."""
    if actor.get_class().get_name() != "StaticMeshActor":
        return False
    c = actor.get_component_by_class(unreal.StaticMeshComponent)
    mesh = c.get_editor_property("static_mesh") if c else None
    return bool(mesh) and mesh.get_path_name().startswith(SKY_DOME_MESH)


def plan(actors):
    """(remove, keep_in_folder): the replaced global lighting actors; other actors of a lighting-looking folder."""
    remove, keep = [], []
    for actor in actors:
        kind = "SkySphere" if is_sky_dome(actor) else replaced_class(actor)
        if kind == "PostProcessVolume" and not safe(lambda: actor.get_editor_property("unbound"), False):
            kind = None  # a bounded volume is a local look: kept
        if kind:
            remove.append((actor, kind))
        elif in_lighting_folder(actor):
            keep.append(actor)
    return remove, keep


def presenter_class():
    """BP_WeatherPresenter when it exists (the user's art tweaks), else the C++ AWeatherPresenterActor."""
    if unreal.EditorAssetLibrary.does_asset_exist(PRESENTER_BP):
        bp = unreal.load_asset(PRESENTER_BP)
        cls = bp.generated_class() if bp else None
        if cls:
            return cls
    return unreal.WeatherPresenterActor


def class_name(cls):
    return cls.get_name() if isinstance(cls, unreal.Class) else cls.static_class().get_name()


def place_presenter(actor_sub, after_removal):
    """One weather presenter in the map (label WeatherPresenter, folder Lighting): it lights the editor viewport as its preview hour /
    weather (OnConstruction) where the level has no lights, and the game finds it instead of spawning one. Skipped with
    CODEX_PLACE_PRESENTER=0 or when the map already has one."""
    if os.environ.get("CODEX_PLACE_PRESENTER", "1") != "1":
        report("presenter: not placed (CODEX_PLACE_PRESENTER=0) - the editor viewport has no light without the level's actors")
        return None
    existing = [a for a in actor_sub.get_all_level_actors() if isinstance(a, unreal.WeatherPresenterActor)]
    if existing:
        report("presenter: kept the placed %s" % existing[0].get_actor_label())
        return existing[0]
    cls = presenter_class()
    actor = actor_sub.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label("WeatherPresenter")
    actor.set_folder_path("Lighting")
    report("presenter: PLACED %s (%s) at the origin in folder 'Lighting' (editor preview: PreviewHour %.1f, PreviewWeather %s)" % (
        actor.get_actor_label(), class_name(cls), actor.get_editor_property("preview_hour"), actor.get_editor_property("preview_weather")))
    return actor


def is_commandlet():
    line = safe(lambda: unreal.SystemLibrary.get_command_line(), "")
    return "-run=" in line.lower()


def backup(files, stamp):
    root = os.path.join(SAVED, "Backups", "remove_level_lighting_" + stamp)
    os.makedirs(root, exist_ok=True)
    manifest = []
    for path in files:
        rel = os.path.relpath(path, CONTENT)
        target = os.path.join(root, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(path, target)
        a, b = sha1(path), sha1(target)
        manifest.append("%s  %s" % (a, rel))
        if a != b:
            raise RuntimeError("backup copy differs: %s (%s vs %s)" % (path, a, b))
        report("backup %s -> %s sha1 %s (verified)" % (path, target, a))
    with open(os.path.join(root, "SHA1SUMS.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(manifest) + "\n")
    return root


def process(package, mode, save_as, stamp):
    lib = unreal.EditorAssetLibrary
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    report("== %s (%s)" % (package, mode))
    if not lib.does_asset_exist(package):
        report("ERROR: %s does not exist" % package)
        return False
    target = package
    if mode == "copy":
        if lib.does_asset_exist(save_as):
            report("copy target %s exists: deleted first -> %s" % (save_as, lib.delete_asset(save_as)))
        if not lib.duplicate_asset(package, save_as):
            report("ERROR: could not duplicate %s -> %s" % (package, save_as))
            return False
        report("duplicated %s -> %s (the source is never saved)" % (package, save_as))
        target = save_as
    if not level_editor.load_level(target):
        report("ERROR: could not load %s" % target)
        return False
    actors = actor_sub.get_all_level_actors()
    remove, keep = plan(actors)
    navmeshes = [a for a in actors if a.get_class().get_name() == "RecastNavMesh"]
    report("actors in the map: %d; navmesh actors: %d" % (len(actors), len(navmeshes)))
    report("-- %d to remove (the presenter replaces them):" % len(remove))
    for actor, kind in remove:
        report("   REMOVE " + describe(actor, kind))
    report("-- %d kept in a lighting folder (not replaced by the presenter; decide by hand):" % len(keep))
    for actor in keep:
        report("   KEEP   " + describe(actor, actor.get_class().get_name()))
    if mode == "dry":
        placed = [a for a in actors if isinstance(a, unreal.WeatherPresenterActor)]
        report("presenter: %s" % ("already placed: %s" % placed[0].get_actor_label() if placed else
            ("would place one WeatherPresenter (%s) in folder 'Lighting'" % class_name(presenter_class())
             if os.environ.get("CODEX_PLACE_PRESENTER", "1") == "1" else "none (CODEX_PLACE_PRESENTER=0)")))
        report("dry run: nothing changed")
        return True
    if mode == "apply" and navmeshes and is_commandlet() and os.environ.get("CODEX_ALLOW_COMMANDLET") != "1":
        report("REFUSED: a commandlet save drops the editor-built navmesh of %s (docs/port/TRAPS.md); run this script in the full editor "
               "(-ExecutePythonScript) or set CODEX_ALLOW_COMMANDLET=1 and bake it afterwards (Scripts/bake_navmesh.ps1 -Map %s)"
               % (package, package.split("/")[-1]))
        return False
    for actor, kind in remove:
        actor_sub.destroy_actor(actor)
    place_presenter(actor_sub, after_removal=True)
    saved = level_editor.save_current_level()
    after = actor_sub.get_all_level_actors()
    left = plan(after)[0]
    report("removed %d actors, saved=%s; replaced lighting actors left: %d; navmesh actors after: %d" % (
        len(remove), saved, len(left), len([a for a in after if a.get_class().get_name() == "RecastNavMesh"])))
    if mode == "apply" and is_commandlet() and navmeshes:
        report("NOTE: saved from a commandlet - bake the navmesh now: powershell -ExecutionPolicy Bypass -File Scripts/bake_navmesh.ps1 -Map %s"
               % package.split("/")[-1])
    return bool(saved)


def main():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    maps = [package_path(m) for m in os.environ.get("CODEX_MAPS", ",".join(DEFAULT_MAPS)).split(",") if m.strip()]
    save_as = package_path(os.environ["CODEX_SAVE_AS"]) if os.environ.get("CODEX_SAVE_AS", "").strip() else ""
    mode = "copy" if save_as else ("apply" if os.environ.get("CODEX_APPLY") == "1" else "dry")
    report("remove_level_lighting.py %s mode=%s maps=%s commandlet=%s" % (datetime.datetime.now().isoformat(), mode, maps, is_commandlet()))
    try:
        if mode == "copy" and len(maps) != 1:
            report("ERROR: CODEX_SAVE_AS needs exactly one map in CODEX_MAPS")
        else:
            if mode == "apply":
                files = [map_file(m) for m in maps]
                missing = [f for f in files if not os.path.isfile(f)]
                if missing:
                    raise RuntimeError("map files not found: %s" % missing)
                report("backup root: %s" % backup(files, stamp))
            ok = all([process(m, mode, save_as, stamp) for m in maps])
            report("RESULT: %s" % ("OK" if ok else "FAILED"))
    except Exception as exc:  # the report is written whatever happens
        report("ERROR: %r" % exc)
    flush()


main()
