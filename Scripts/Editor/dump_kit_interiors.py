"""Read-only horizontal slices of the Soviet Houses kit buildings (floor plans).

For each /Game/SovietHouses/Maps/Buildings/L_SH_Building_NN map, picks the ground storey
(lowest placed SM_SH_Bxx_Interior_F* actor) and one typical upper storey (the median
placement of the most common upper Interior mesh). For each storey it cuts every wall-like
placed mesh (Interior_F*, facade Wall_ / Rust_ / StairWall_ / StairEnt_ / IntWall_ modules,
StairGlass, windows) with the plane z = floor + CUT_HEIGHT_CM and stores the 2D segments.
It also stores the slab footprint under that storey (upward faces projected to XY) and the
world AABB of every other placed mesh in the storey's height band (doors, stairs, balconies).

Output: Saved/KitDump/kit_interiors.json of the project it runs in (CODEX_KIT_OUT overrides).
Never saves, checks out or modifies any asset; dirty flags are cleared before exit.

  $env:CODEX_KIT_ONLY = "01"   # optional, one building
  UnrealEditor-Cmd.exe "<agents>\\CodexTactics.uproject" -run=pythonscript "-Script=\"<this file>\"" -unattended -nullrhi -nosplash -log

Triangles come from ProceduralMeshLibrary.get_section_from_static_mesh (LOD0, every
section); when that returns nothing the StaticMeshDescription of LOD0 is read instead.
Pure plane / triangle math lives in slice_triangles() and is unit-tested from
Scripts/Tools/tests/test_kit_plan_images.py through the same algorithm in
Scripts/Tools/kit_plan_images.py.
"""
import json
import os
import re
import statistics

import unreal


MAP_DIR = "/Game/SovietHouses/Maps/Buildings"
BUILDING_COUNT = 20
CUT_HEIGHT_CM = 100.0
INTERIOR = re.compile(r"_Interior_F(\d+)$")
SLAB = re.compile(r"_Slab_F(\d+)$")
# Meshes cut by the plane. Everything else in the storey band is stored as an AABB item.
SLICE_TOKENS = ("_Interior_F", "SM_SH_Wall_", "SM_SH_Rust_", "SM_SH_StairWall_", "SM_SH_StairEnt_",
                "SM_SH_IntWall_", "SM_SH_StairGlass_", "SM_SH_Win_", "SM_SH_Surround_", "SM_SH_Pilaster_")
SKIP_ITEM_TOKENS = ("_Slab_F", "_Interior_F", "_Roof_", "SM_SH_Drain_", "SM_SH_GasPipe", "SM_SH_Cornice_",
                    "SM_SH_Belt_", "SM_SH_Parapet_", "SM_SH_Chimney_", "SM_SH_Roof_")


def log(message):
    print("kit interiors: %s" % message)  # print already reaches LogPython


def short_error(exc):
    return ("%s: %s" % (type(exc).__name__, exc)).replace("\n", " ")[:300]


def r2(value):
    return round(float(value), 2)


# ---------------------------------------------------------------- pure math

def slice_triangles(triangles, cut_z, eps=1e-4):
    """Segments where plane z = cut_z crosses triangles [(a, b, c)] of (x, y, z) points.

    A vertex exactly on the plane is nudged up by eps so a shared edge is cut once.
    Faces lying in the plane give no segment. Each segment runs along (-ny, nx) of its
    triangle's winding normal, so a consistently wound closed mesh gives loops of one
    orientation (outer boundaries one way, holes the other).
    """
    segments = []
    for tri in triangles:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = tri
        nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
        ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az)
        d = []
        for p in tri:
            value = p[2] - cut_z
            if abs(value) < eps:
                value = eps
            d.append(value)
        points = []
        for i in range(3):
            j = (i + 1) % 3
            if (d[i] > 0.0) != (d[j] > 0.0):
                t = d[i] / (d[i] - d[j])
                a = tri[i]
                b = tri[j]
                points.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
        if len(points) == 2:
            (x0, y0), (x1, y1) = points
            if (x1 - x0) * -ny + (y1 - y0) * nx < 0.0:
                x0, y0, x1, y1 = x1, y1, x0, y0
            if abs(x0 - x1) + abs(y0 - y1) > 1e-3:
                segments.append((x0, y0, x1, y1))
    return segments


def upward_triangles_xy(triangles, min_nz=0.5):
    """XY projection of triangles whose unit normal has |z| >= min_nz (slab plates)."""
    flat = []
    for a, b, c in triangles:
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        length = (nx * nx + ny * ny + nz * nz) ** 0.5
        if length <= 1e-9 or abs(nz) / length < min_nz:
            continue
        flat.append(((a[0], a[1]), (b[0], b[1]), (c[0], c[1])))
    return flat


# ---------------------------------------------------------------- unreal access

def load_building_map(path):
    try:
        if unreal.EditorLoadingAndSavingUtils.load_map(path):
            return True, None
    except Exception as exc:
        first = short_error(exc)
    else:
        first = "load_map returned false"
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path):
            return True, None
    except Exception as exc:
        return False, "%s | %s" % (first, short_error(exc))
    return False, first


def all_level_actors():
    try:
        actors = list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
        if actors:
            return actors
    except Exception:
        pass
    return list(unreal.EditorLevelLibrary.get_all_level_actors())


def section_count(mesh, lod):
    for getter in (lambda: mesh.get_num_sections(lod),
                   lambda: unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_number_sections(mesh, lod)):
        try:
            count = int(getter())
            if count > 0:
                return count
        except Exception:
            continue
    return 1


def local_triangles_procmesh(mesh):
    library = getattr(unreal, "KismetProceduralMeshLibrary", None) or getattr(unreal, "ProceduralMeshLibrary", None)
    if library is None:
        return []
    triangles = []
    for section in range(section_count(mesh, 0)):
        result = library.get_section_from_static_mesh(mesh, 0, section)
        vertices, indices = result[0], result[1]
        vertices = [(v.x, v.y, v.z) for v in vertices]
        for i in range(0, len(indices) - 2, 3):
            triangles.append((vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]]))
    return triangles


def local_triangles_description(mesh):
    description = mesh.get_static_mesh_description(0)
    if description is None:
        return []
    triangles = []
    count = int(description.get_triangle_count())
    for index in range(count):
        tid = unreal.TriangleID()
        tid.id_value = index
        try:
            vids = description.get_triangle_vertices(tid)
        except Exception:
            continue
        points = []
        for vid in vids:
            p = description.get_vertex_position(vid)
            points.append((p.x, p.y, p.z))
        if len(points) == 3:
            triangles.append(tuple(points))
    return triangles


def local_triangles(mesh, cache, errors):
    key = mesh.get_path_name()
    if key in cache:
        return cache[key]
    triangles = []
    method = None
    try:
        triangles = local_triangles_procmesh(mesh)
        method = "procmesh"
    except Exception as exc:
        errors.append("%s procmesh: %s" % (mesh.get_name(), short_error(exc)))
    if not triangles:
        try:
            triangles = local_triangles_description(mesh)
            method = "description"
        except Exception as exc:
            errors.append("%s description: %s" % (mesh.get_name(), short_error(exc)))
    if not triangles:
        errors.append("%s: no triangles" % mesh.get_name())
    cache[key] = triangles
    cache.setdefault("_methods", {})[key] = method
    return triangles


def affine_of(transform):
    """(origin, x column, y column, z column) of the transform, scale included."""
    def at(x, y, z):
        w = unreal.MathLibrary.transform_location(transform, unreal.Vector(x, y, z))
        return (w.x, w.y, w.z)
    o = at(0.0, 0.0, 0.0)
    cols = []
    for axis in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)):
        p = at(*axis)
        cols.append((p[0] - o[0], p[1] - o[1], p[2] - o[2]))
    return o, cols[0], cols[1], cols[2]


def world_triangles(local, transform):
    o, cx, cy, cz = affine_of(transform)
    out = []
    for tri in local:
        points = []
        for x, y, z in tri:
            points.append((o[0] + cx[0] * x + cy[0] * y + cz[0] * z,
                           o[1] + cx[1] * x + cy[1] * y + cz[1] * z,
                           o[2] + cx[2] * x + cy[2] * y + cz[2] * z))
        out.append(tuple(points))
    return out


def world_aabb(component, mesh):
    """World AABB from the mesh's local box and the component transform."""
    box = mesh.get_bounding_box()
    corners = [(x, y, z) for x in (box.min.x, box.max.x) for y in (box.min.y, box.max.y) for z in (box.min.z, box.max.z)]
    points = world_triangles([corners[0:3], corners[3:6], corners[5:8]], component.get_world_transform())
    flat = [p for tri in points for p in tri]
    return ([min(p[0] for p in flat), min(p[1] for p in flat), min(p[2] for p in flat)],
            [max(p[0] for p in flat), max(p[1] for p in flat), max(p[2] for p in flat)])


def mesh_components():
    rows = []
    for actor in all_level_actors():
        try:
            components = list(actor.get_components_by_class(unreal.StaticMeshComponent))
        except Exception:
            continue
        for component in components:
            mesh = component.get_editor_property("static_mesh")
            if mesh is None:
                continue
            low, high = world_aabb(component, mesh)
            rows.append({"component": component, "mesh": mesh, "name": mesh.get_name(),
                         "actor": actor.get_actor_label() if hasattr(actor, "get_actor_label") else actor.get_name(),
                         "low": low, "high": high})
    return rows


# ---------------------------------------------------------------- storey choice

def choose_storeys(rows):
    """[(kind, interior_row)] for ground and one typical upper storey."""
    interiors = [row for row in rows if INTERIOR.search(row["name"])]
    if not interiors:
        return []
    interiors.sort(key=lambda row: row["component"].get_world_transform().translation.z)
    chosen = [("ground", interiors[0])]
    upper = interiors[1:]
    if upper:
        counts = {}
        for row in upper:
            counts[row["name"]] = counts.get(row["name"], 0) + 1
        common = max(counts, key=lambda name: (counts[name], name))
        same = [row for row in upper if row["name"] == common]
        chosen.append(("upper", same[(len(same) - 1) // 2]))
    return chosen


def storey_height(rows):
    zs = sorted(row["component"].get_world_transform().translation.z for row in rows if INTERIOR.search(row["name"]))
    gaps = [b - a for a, b in zip(zs, zs[1:]) if b - a > 50.0]
    return statistics.median(gaps) if gaps else 280.0


def slab_under(rows, floor_z):
    slabs = [row for row in rows if SLAB.search(row["name"])]
    if not slabs:
        return None
    return min(slabs, key=lambda row: abs(row["high"][2] - floor_z))


def is_slice_mesh(name):
    return any(token in name for token in SLICE_TOKENS)


def is_item_mesh(name):
    return not any(token in name for token in SKIP_ITEM_TOKENS)


def dump_storey(kind, interior, rows, height_cm, cache, errors):
    floor_z = interior["component"].get_world_transform().translation.z
    cut_z = floor_z + CUT_HEIGHT_CM
    band_low, band_high = floor_z - 20.0, floor_z + height_cm - 5.0
    walls = []
    items = []
    for row in rows:
        low, high = row["low"], row["high"]
        name = row["name"]
        if is_slice_mesh(name) and low[2] < cut_z < high[2]:
            if INTERIOR.search(name) and row is not interior:
                continue
            local = local_triangles(row["mesh"], cache, errors)
            segments = slice_triangles(world_triangles(local, row["component"].get_world_transform()), cut_z)
            if segments:
                walls.append({"mesh": name, "actor": row["actor"],
                              "segments": [[r2(v) for v in seg] for seg in segments]})
        if is_item_mesh(name) and high[2] > band_low and low[2] < band_high:
            items.append({"mesh": name, "min": [r2(v) for v in low], "max": [r2(v) for v in high]})
    slab = slab_under(rows, floor_z)
    footprint = []
    if slab is not None:
        local = local_triangles(slab["mesh"], cache, errors)
        flat = upward_triangles_xy(world_triangles(local, slab["component"].get_world_transform()))
        footprint = [[[r2(p[0]), r2(p[1])] for p in tri] for tri in flat]
    return {
        "kind": kind,
        "interior_mesh": interior["name"],
        "interior_actor": interior["actor"],
        "floor_z": r2(floor_z),
        "cut_z": r2(cut_z),
        "slab_mesh": None if slab is None else slab["name"],
        "footprint_triangles": footprint,
        "walls": walls,
        "items": items,
    }


def dump_building(building_id, cache, errors):
    path = "%s/L_SH_Building_%s" % (MAP_DIR, building_id)
    entry = {"id": building_id, "map": path, "storeys": []}
    log("loading %s" % path)
    loaded, error = load_building_map(path)
    if not loaded:
        entry["error"] = error
        errors.append("%s: %s" % (path, error))
        return entry
    rows = mesh_components()
    height = storey_height(rows)
    entry["storey_height_cm"] = r2(height)
    entry["interior_placements"] = len([row for row in rows if INTERIOR.search(row["name"])])
    for kind, interior in choose_storeys(rows):
        try:
            entry["storeys"].append(dump_storey(kind, interior, rows, height, cache, errors))
        except Exception as exc:
            errors.append("%s %s: %s" % (path, kind, short_error(exc)))
    log("%s storeys=%s segments=%s" % (path, [s["kind"] for s in entry["storeys"]],
                                      [sum(len(w["segments"]) for w in s["walls"]) for s in entry["storeys"]]))
    return entry


def output_path():
    override = os.environ.get("CODEX_KIT_OUT")
    if override:
        return os.path.abspath(override)
    root = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.join(root, "Saved", "KitDump", "kit_interiors.json")


def forget_dirty_packages():
    try:
        packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    except Exception:
        return 0
    cleared = 0
    for package in packages:
        try:
            package.set_dirty_flag(False)
            cleared += 1
        except Exception:
            continue
    return cleared


def main():
    # Read-only: no save_map, save_asset or save_dirty_packages anywhere in this file.
    only = (os.environ.get("CODEX_KIT_ONLY") or "").strip()
    ids = ["%02d" % int(only)] if only.isdigit() else ["%02d" % n for n in range(1, BUILDING_COUNT + 1)]
    errors = []
    cache = {}
    buildings = []
    for building_id in ids:
        try:
            buildings.append(dump_building(building_id, cache, errors))
        except Exception as exc:
            errors.append("%s: %s" % (building_id, short_error(exc)))
            buildings.append({"id": building_id, "error": short_error(exc), "storeys": []})
    methods = cache.get("_methods", {})
    payload = {
        "schema": "codex.kit_interiors/1",
        "units": "cm",
        "cut_height_cm": CUT_HEIGHT_CM,
        "triangle_methods": sorted({str(m) for m in methods.values()}),
        "buildings": buildings,
        "errors": errors[:200],
    }
    destination = output_path()
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle)
        handle.write("\n")
    cleared = forget_dirty_packages()
    log("wrote %s buildings=%d errors=%d methods=%s dirty_cleared=%d (nothing saved)" % (
        destination, len(buildings), len(errors), payload["triangle_methods"], cleared))


main()
