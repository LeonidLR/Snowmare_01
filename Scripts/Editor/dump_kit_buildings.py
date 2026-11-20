"""Read-only dump of the Soviet Houses kit (G013).

Loads each /Game/SovietHouses/Maps/Buildings/L_SH_Building_NN map, lists every placed
static mesh (StaticMeshActor, instanced mesh, Blueprint StaticMeshComponent) and the
local bounds of every StaticMesh under /Game/SovietHouses/Meshes. Writes
Saved/KitDump/kit_buildings.json. Does not save, check out, or modify any asset.

Run on the agents worktree (the pack is untracked there). One building:

  $env:CODEX_KIT_ONLY = "01"
  UnrealEditor-Cmd.exe "<agents>\\CodexTactics.uproject" -run=pythonscript -script="<agents>\\Scripts\\Editor\\dump_kit_buildings.py" -unattended -nullrhi -nosplash -log

--only is also read from sys.argv when the commandlet forwards it. The form UE 5.8
accepts for a path that contains spaces is a quoted script plus the argument outside
that quote, passed as the whole -Script= value:

  -Script="\"<agents>\\Scripts\\Editor\\dump_kit_buildings.py\" --only 01"

CODEX_KIT_OUT overrides the json path. CODEX_KIT_ONLY overrides nothing if --only is set.
"""
import json
import os
import sys

import unreal


MAP_DIR = "/Game/SovietHouses/Maps/Buildings"
MESH_ROOT = "/Game/SovietHouses/Meshes"
BUILDING_COUNT = 20


def log(message):
    text = "G013 kit dump: %s" % message
    print(text)
    try:
        unreal.log(text)
    except Exception:
        pass


def short_error(exc):
    text = "%s: %s" % (type(exc).__name__, exc)
    return text.replace("\n", " ")[:300]


def round_list(values):
    return [round(float(v), 2) for v in values]


def vec3(vector):
    return round_list((vector.x, vector.y, vector.z))


def rot3(rotator):
    return round_list((rotator.pitch, rotator.yaw, rotator.roll))


def mesh_identity(mesh):
    full = mesh.get_path_name()
    package = full.split(".")[0]
    return package, mesh.get_name()


def read_mesh_box(mesh):
    """Local axis-aligned box in cm. Tries the tight box, then the bounds sphere box."""
    if hasattr(mesh, "get_bounding_box"):
        box = mesh.get_bounding_box()
        return box.min, box.max
    bounds = mesh.get_bounds()
    origin = bounds.origin
    extent = bounds.box_extent
    return (
        unreal.Vector(origin.x - extent.x, origin.y - extent.y, origin.z - extent.z),
        unreal.Vector(origin.x + extent.x, origin.y + extent.y, origin.z + extent.z),
    )


def box_record(mesh):
    box_min, box_max = read_mesh_box(mesh)
    size = (
        abs(box_max.x - box_min.x),
        abs(box_max.y - box_min.y),
        abs(box_max.z - box_min.z),
    )
    return {
        "size": round_list(size),
        "bounds_min": vec3(box_min),
        "bounds_max": vec3(box_max),
    }


def asset_class_name(data):
    class_path = getattr(data, "asset_class_path", None)
    if class_path is None:
        return str(getattr(data, "asset_class", ""))
    return str(class_path.asset_name)


def dump_mesh_catalog(errors):
    """Every StaticMesh under the kit Meshes folder. Load is read-only; nothing is saved."""
    rows = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        registry.scan_paths_synchronous([MESH_ROOT], True)
    except Exception as exc:
        errors.append("mesh scan: %s" % short_error(exc))
        return rows
    try:
        assets = list(registry.get_assets_by_path(MESH_ROOT, recursive=True))
    except Exception as exc:
        errors.append("mesh list: %s" % short_error(exc))
        return rows
    for data in assets:
        if asset_class_name(data) != "StaticMesh":
            continue
        package = str(data.package_name)
        name = str(data.asset_name)
        row = {"path": package, "name": name}
        try:
            mesh = data.get_asset()
            if mesh is None:
                raise RuntimeError("get_asset returned None")
            row.update(box_record(mesh))
        except Exception as exc:
            row["error"] = short_error(exc)
            errors.append("%s: %s" % (package, row["error"]))
        rows.append(row)
    rows.sort(key=lambda row: row["path"])
    return rows


def script_only_argument(argv):
    """--only NN from argv. Ignores engine tokens the commandlet may also forward."""
    only = None
    index = 0
    while index < len(argv):
        token = argv[index]
        if token == "--only" and index + 1 < len(argv):
            only = argv[index + 1]
            index += 2
            continue
        if token.startswith("--only="):
            only = token.split("=", 1)[1]
            index += 1
            continue
        index += 1
    return only


def building_ids(only_text):
    if only_text is None or str(only_text).strip() == "":
        return ["%02d" % number for number in range(1, BUILDING_COUNT + 1)]
    text = str(only_text).strip()
    if not text.isdigit():
        raise SystemExit("G013 kit dump: --only expects a building number 1..20, got %r" % only_text)
    number = int(text)
    if number < 1 or number > BUILDING_COUNT:
        raise SystemExit("G013 kit dump: --only expects a building number 1..20, got %s" % number)
    return ["%02d" % number]


def map_path(building_id):
    return "%s/L_SH_Building_%s" % (MAP_DIR, building_id)


def load_building_map(path):
    """Load a map for reading. Never saves the previous or the loaded map."""
    first_error = None
    try:
        loaded = unreal.EditorLoadingAndSavingUtils.load_map(path)
        if loaded:
            return True, None
        first_error = "load_map returned %r" % (loaded,)
    except Exception as exc:
        first_error = short_error(exc)
    try:
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if subsystem.load_level(path):
            return True, None
        return False, first_error or "LevelEditorSubsystem.load_level returned false"
    except Exception as exc:
        extra = short_error(exc)
        combined = "%s | %s" % (first_error, extra) if first_error else extra
        return False, combined


def all_level_actors():
    try:
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = list(subsystem.get_all_level_actors())
        if actors:
            return actors
    except Exception:
        pass
    return list(unreal.EditorLevelLibrary.get_all_level_actors())


def is_instanced(component):
    classes = []
    for class_name in ("InstancedStaticMeshComponent", "HierarchicalInstancedStaticMeshComponent"):
        component_class = getattr(unreal, class_name, None)
        if component_class is not None:
            classes.append(component_class)
    return any(isinstance(component, component_class) for component_class in classes)


def instance_transform(component, index):
    """World transform of one instance. UE Python may return the transform or (ok, transform)."""
    result = component.get_instance_transform(index, True)
    if isinstance(result, tuple):
        if len(result) >= 2 and isinstance(result[0], bool):
            if not result[0]:
                raise RuntimeError("get_instance_transform failed for %s" % index)
            return result[1]
        return result[-1]
    return result


def transformed_aabb(box_min, box_max, transform):
    points = []
    for x in (box_min.x, box_max.x):
        for y in (box_min.y, box_max.y):
            for z in (box_min.z, box_max.z):
                points.append(unreal.MathLibrary.transform_location(transform, unreal.Vector(x, y, z)))
    return (
        [min(point.x for point in points), min(point.y for point in points), min(point.z for point in points)],
        [max(point.x for point in points), max(point.y for point in points), max(point.z for point in points)],
    )


def component_aabb(component, mesh):
    """World AABB. Prefer the engine bounds; rebuild from the mesh box if they are empty."""
    try:
        bounds = component.bounds
        extent = bounds.box_extent
        if abs(extent.x) + abs(extent.y) + abs(extent.z) > 0.01:
            origin = bounds.origin
            return (
                [origin.x - extent.x, origin.y - extent.y, origin.z - extent.z],
                [origin.x + extent.x, origin.y + extent.y, origin.z + extent.z],
            )
    except Exception:
        pass
    box_min, box_max = read_mesh_box(mesh)
    return transformed_aabb(box_min, box_max, component.get_world_transform())


def transform_record(transform):
    rotator = transform.rotation.rotator()
    return vec3(transform.translation), rot3(rotator), vec3(transform.scale3d)


def placed_record(actor, component, mesh, transform, bounds_min, bounds_max, instance_index):
    package, mesh_name = mesh_identity(mesh)
    location, rotation, scale = transform_record(transform)
    label = actor.get_name()
    try:
        label = actor.get_actor_label() or label
    except Exception:
        pass
    return {
        "actor": label,
        "actor_class": actor.get_class().get_name(),
        "component_class": component.get_class().get_name(),
        "instance": instance_index,
        "mesh": package,
        "mesh_name": mesh_name,
        "location": location,
        "rotation": rotation,
        "scale": scale,
        "bounds_min": round_list(bounds_min),
        "bounds_max": round_list(bounds_max),
    }


def records_for_component(actor, component, cache, errors):
    mesh = component.get_editor_property("static_mesh")
    if mesh is None:
        return []
    package, _mesh_name = mesh_identity(mesh)
    if package not in cache:
        try:
            cache[package] = read_mesh_box(mesh)
        except Exception as exc:
            cache[package] = exc
    records = []
    if is_instanced(component):
        count = int(component.get_instance_count())
        for index in range(count):
            try:
                transform = instance_transform(component, index)
                box = cache[package]
                if isinstance(box, Exception):
                    raise box
                bounds_min, bounds_max = transformed_aabb(box[0], box[1], transform)
                records.append(placed_record(
                    actor, component, mesh, transform, bounds_min, bounds_max, index))
            except Exception as exc:
                errors.append("%s instance %s: %s" % (actor.get_name(), index, short_error(exc)))
        return records
    try:
        transform = component.get_world_transform()
        bounds_min, bounds_max = component_aabb(component, mesh)
        records.append(placed_record(
            actor, component, mesh, transform, bounds_min, bounds_max, None))
    except Exception as exc:
        errors.append("%s: %s" % (actor.get_name(), short_error(exc)))
    return records


def dump_loaded_map(errors):
    """Mesh records for the map that is loaded right now. One bad actor does not stop the map."""
    records = []
    try:
        actors = all_level_actors()
    except Exception as exc:
        errors.append("actor list: %s" % short_error(exc))
        return records
    cache = {}
    for actor in actors:
        try:
            components = list(actor.get_components_by_class(unreal.StaticMeshComponent))
        except Exception as exc:
            errors.append("%s components: %s" % (actor.get_name(), short_error(exc)))
            continue
        for component in components:
            try:
                records.extend(records_for_component(actor, component, cache, errors))
            except Exception as exc:
                errors.append("%s: %s" % (actor.get_name(), short_error(exc)))
    return records


def world_path():
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        if world is None:
            return None
        return world.get_path_name().split(".")[0]
    except Exception:
        return None


def dump_buildings(ids, errors):
    buildings = []
    for building_id in ids:
        path = map_path(building_id)
        entry = {
            "id": building_id,
            "map": path,
            "loaded": False,
            "world": None,
            "actors": [],
            "actor_errors": [],
        }
        log("loading %s" % path)
        try:
            loaded, load_error = load_building_map(path)
        except Exception as exc:
            loaded, load_error = False, short_error(exc)
        if not loaded:
            entry["error"] = load_error or "load failed"
            errors.append("%s: %s" % (path, entry["error"]))
            log("%s LOAD FAILED %s" % (path, entry["error"]))
            buildings.append(entry)
            continue
        entry["loaded"] = True
        entry["world"] = world_path()
        map_errors = []
        try:
            entry["actors"] = dump_loaded_map(map_errors)
        except Exception as exc:
            map_errors.append(short_error(exc))
        entry["actor_errors"] = map_errors
        errors.extend("%s: %s" % (path, item) for item in map_errors)
        log("%s actors=%d mesh_records=%d errors=%d" % (
            path, len({row["actor"] for row in entry["actors"]}), len(entry["actors"]), len(map_errors)))
        buildings.append(entry)
    return buildings


def output_path():
    override = os.environ.get("CODEX_KIT_OUT")
    if override:
        return os.path.abspath(override)
    root = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.join(root, "Saved", "KitDump", "kit_buildings.json")


def dirty_packages():
    try:
        return list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    except Exception:
        return None


def forget_dirty_packages():
    """Clear the in-memory dirty bit. A commandlet exit must not write the kit."""
    packages = dirty_packages()
    if not packages:
        return 0
    cleared = 0
    for package in packages:
        setter = getattr(package, "set_dirty_flag", None)
        if setter is None:
            continue
        try:
            setter(False)
            cleared += 1
        except Exception:
            continue
    return cleared


def main(argv=None):
    # Read-only: no save_map, save_current_level, save_asset, or save_dirty_packages.
    only_text = script_only_argument(sys.argv[1:] if argv is None else argv)
    if only_text is None:
        only_text = os.environ.get("CODEX_KIT_ONLY")
    ids = building_ids(only_text)
    errors = []
    log("meshes under %s" % MESH_ROOT)
    meshes = dump_mesh_catalog(errors)
    log("mesh assets=%d" % len(meshes))
    buildings = dump_buildings(ids, errors)
    payload = {
        "schema": "codex.kit_buildings/1",
        "units": "cm",
        "mesh_root": MESH_ROOT,
        "buildings": buildings,
        "meshes": meshes,
        "errors": errors,
    }
    destination = output_path()
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")
    dirty_before = dirty_packages()
    dirty_count = None if dirty_before is None else len(dirty_before)
    cleared = forget_dirty_packages()
    log("wrote %s buildings=%d meshes=%d errors=%d dirty_before=%s dirty_cleared=%d (nothing saved)" % (
        destination, len(buildings), len(meshes), len(errors), dirty_count, cleared))
    return 0


if __name__ == "__main__":
    main()
