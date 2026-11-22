"""Turn Saved/KitDump/kit_buildings.json into a floor-plan reading of the Soviet kit.

Pure Python. No Unreal. The editor dump is Scripts/Editor/dump_kit_buildings.py.

Heuristics ( centimetres in, metres in the footprint ):

- Footprint is the XY union of Slab_F meshes. Without slabs, wall / roof / stair meshes.
- Storeys are the distinct Slab_F indices. Storey height is the median gap between
  consecutive floors' bounds_min.z.
- An entrance is Ent_Door. A stairwell clusters Stair_*, StairGlass_*, IntWall_stair*
  and Steps_* within 250 cm. StairEnt and StairWall are facade pieces, not the well.
- Street is the long footprint edge with more Balc_* volumes (a tie prefers the max edge).
  The opposite long edge is court. Short edges are ends. This is a guess.
- Flat doors are Door_Apt. IntWall_aptdoor counts only when the building has no Door_Apt,
  because the wall module and the leaf sit on the same opening.
- Flats per landing is the median Door_Apt count on each (stairwell, slab floor) that
  has at least one door. Doors go to the nearest stairwell.
- Rooms per flat is 1 + (IntWall_part and IntWall_partdoor modules) / flat doors.
- Windows are Win_* meshes, grouped by facade. Width is the placed extent along that
  facade. Wall_* / Rust_* modules are counted apart, by opening token (win, shop, solid).
  A _28 / _30 / _32 suffix is the nominal width in decimetres.
- Shop pieces have "shop" in the name. shopdoor is a door; the rest are window modules.
- Roof is pitched_dormer when a Dormer is placed, otherwise flat when a Roof or Parapet
  is placed.
- Grid: each placed XY extent thicker than 40 cm is tested at its min, max and width
  against 300 cm and 50 cm, measured from the footprint minimum. Residual within 2 cm fits.
  The mesh catalog repeats the check on each local size thicker than 40 cm.

  python Scripts/Tools/kit_plan_report.py --input Saved/KitDump/kit_buildings.json --out-dir Saved/KitDump
"""
import argparse
import json
import os
import re
import statistics

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_NAME = "kit_buildings.json"
GRID_STEPS_CM = (300.0, 50.0)
GRID_TOLERANCE_CM = 2.0
THIN_CM = 40.0
STAIR_CLUSTER_CM = 250.0
NOMINAL_WIDTH = re.compile(r"_(\d{2})(?=_|$)")
SLAB_FLOOR = re.compile(r"Slab_F(\d+)")
BUILDING_ID = re.compile(r"Building_(\d+)")
WALL_FAMILIES = ("PanelTile", "BrickR", "BrickW", "Plaster", "Planks", "Facade", "Panel")
EDGES = ("min_x", "max_x", "min_y", "max_y")


def default_input_path():
    local = os.path.join(REPO_ROOT, "Saved", "KitDump", DEFAULT_NAME)
    if os.path.isfile(local):
        return local
    sibling = os.path.normpath(os.path.join(
        REPO_ROOT, "..", "CodexTactics-agents", "Saved", "KitDump", DEFAULT_NAME))
    if os.path.isfile(sibling):
        return sibling
    return local


def mesh_name_of(record):
    name = record.get("mesh_name") or ""
    if name:
        return str(name)
    mesh = str(record.get("mesh") or record.get("name") or "")
    base = mesh.replace("\\", "/").split("/")[-1]
    return base.split(".")[0]


def bounds_of(record):
    low = record.get("bounds_min")
    high = record.get("bounds_max")
    if not low or not high or len(low) < 3 or len(high) < 3:
        return None
    return [float(low[0]), float(low[1]), float(low[2])], [float(high[0]), float(high[1]), float(high[2])]


def center(low, high):
    return [(low[axis] + high[axis]) * 0.5 for axis in range(3)]


def nominal_width_cm(name):
    """Kit suffix _28 means 280 cm. Values outside 20..40 are not module widths."""
    found = [int(token) for token in NOMINAL_WIDTH.findall(name)]
    for value in reversed(found):
        if 20 <= value <= 40:
            return value * 10
    return None


def is_entrance(name):
    return "Ent_Door" in name


def is_stairwell_piece(name):
    if "StairEnt" in name or "StairWall" in name:
        return False
    return (
        "StairGlass" in name
        or "IntWall_stair" in name
        or "_Steps_" in name
        or name.startswith("SM_SH_Stair")
        or "_Stair_" in name
    )


def is_flat_door_leaf(name):
    return "Door_Apt" in name


def is_flat_door_wall(name):
    return "IntWall_aptdoor" in name


def is_partition(name):
    return "IntWall_part" in name


def is_balcony_volume(name):
    return "Balc_" in name


def is_window(name):
    return "Win_" in name or "_Win_" in name


def is_shop_door(name):
    return "shopdoor" in name.lower()


def is_shop_window(name):
    lowered = name.lower()
    return "shop" in lowered and "shopdoor" not in lowered


def wall_family(name):
    if "Wall_" not in name and "Rust_" not in name:
        return None
    for family in WALL_FAMILIES:
        token = "_%s_" % family
        if token in name:
            return family
    return None


def facade_kind(name):
    """Opening token of a wall module: win, solid, shop, shopdoor, balc, door, ..."""
    family = wall_family(name)
    if family is None:
        return None
    token = "_%s_" % family
    index = name.find(token)
    if index < 0:
        return None
    rest = NOMINAL_WIDTH.sub("", name[index + len(token):]).strip("_")
    return rest or "plain"


def window_type(name):
    text = name
    if text.startswith("SM_SH_"):
        text = text[len("SM_SH_"):]
    if text.startswith("Win_"):
        text = text[len("Win_"):]
    text = NOMINAL_WIDTH.sub("", text).strip("_")
    return text or name


def union_xy(records):
    boxes = [bounds_of(record) for record in records]
    boxes = [box for box in boxes if box]
    if not boxes:
        return None
    return (
        min(box[0][0] for box in boxes),
        min(box[0][1] for box in boxes),
        max(box[1][0] for box in boxes),
        max(box[1][1] for box in boxes),
    )


def footprint_records(actors):
    slabs = [actor for actor in actors if "Slab_F" in mesh_name_of(actor)]
    if slabs:
        return slabs, "slabs"
    structural = []
    for actor in actors:
        name = mesh_name_of(actor)
        if any(token in name for token in ("Wall_", "Roof", "Stair", "Parapet", "Plinth")):
            structural.append(actor)
    if structural:
        return structural, "walls"
    return list(actors), "all"


def slab_floors(actors):
    """One entry per Slab_F index. z is the mean underside of that floor's slabs."""
    grouped = {}
    for actor in actors:
        match = SLAB_FLOOR.search(mesh_name_of(actor))
        box = bounds_of(actor)
        if not match or not box:
            continue
        index = int(match.group(1))
        grouped.setdefault(index, []).append(box[0][2])
    floors = []
    for index in sorted(grouped):
        floors.append({"index": index, "z": statistics.fmean(grouped[index])})
    return floors


def storey_height_cm(floors):
    if len(floors) < 2:
        return None, []
    gaps = [floors[index + 1]["z"] - floors[index]["z"] for index in range(len(floors) - 1)]
    return statistics.median(gaps), [round(gap, 1) for gap in gaps]


def nearest_edge(x, y, footprint):
    min_x, min_y, max_x, max_y = footprint
    distances = {
        "min_x": abs(x - min_x),
        "max_x": abs(x - max_x),
        "min_y": abs(y - min_y),
        "max_y": abs(y - max_y),
    }
    return min(EDGES, key=lambda edge: (distances[edge], EDGES.index(edge)))


def along_fraction(edge, x, y, footprint):
    min_x, min_y, max_x, max_y = footprint
    if edge in ("min_y", "max_y"):
        span = max_x - min_x
        return 0.0 if span <= 0 else (x - min_x) / span
    span = max_y - min_y
    return 0.0 if span <= 0 else (y - min_y) / span


def facade_axis_width(low, high, edge):
    if edge in ("min_y", "max_y"):
        return high[0] - low[0]
    return high[1] - low[1]


def classify_edges(footprint, balcony_counts):
    """Long edge with the most balcony volumes is street. Opposite long edge is court."""
    min_x, min_y, max_x, max_y = footprint
    width = max_x - min_x
    depth = max_y - min_y
    long_edges = ("min_y", "max_y") if width >= depth else ("min_x", "max_x")
    short_edges = tuple(edge for edge in EDGES if edge not in long_edges)

    def score(edge):
        return (balcony_counts.get(edge, 0), 1 if edge.startswith("max") else 0)

    street = max(long_edges, key=score)
    court = next(edge for edge in long_edges if edge != street)
    labels = {street: "street", court: "court"}
    for edge in short_edges:
        labels[edge] = "end"
    return labels


def cluster_stairwells(pieces):
    groups = []
    for piece in pieces:
        placed = False
        for group in groups:
            count = len(group["xs"])
            cx = sum(group["xs"]) / count
            cy = sum(group["ys"]) / count
            if (cx - piece["x"]) ** 2 + (cy - piece["y"]) ** 2 <= STAIR_CLUSTER_CM ** 2:
                group["xs"].append(piece["x"])
                group["ys"].append(piece["y"])
                group["names"].append(piece["name"])
                placed = True
                break
        if not placed:
            groups.append({"xs": [piece["x"]], "ys": [piece["y"]], "names": [piece["name"]]})
    stairwells = []
    for index, group in enumerate(groups):
        count = len(group["xs"])
        stairwells.append({
            "index": index,
            "x": sum(group["xs"]) / count,
            "y": sum(group["ys"]) / count,
            "members": count,
            "meshes": sorted(set(group["names"])),
        })
    return stairwells


def nearest_floor(z, floors):
    if not floors:
        return None
    return min(floors, key=lambda floor: abs(floor["z"] - z))["index"]


def nearest_stairwell(x, y, stairwells):
    if not stairwells:
        return 0
    return min(stairwells, key=lambda well: (well["x"] - x) ** 2 + (well["y"] - y) ** 2)["index"]


def as_count(value):
    if value is None:
        return None
    if abs(value - round(value)) < 1e-6:
        return int(round(value))
    return round(float(value), 2)


def grid_residual(value, step):
    remainder = abs(value) % step
    return min(remainder, step - remainder)


def grid_fits(value, step):
    return grid_residual(value, step) <= GRID_TOLERANCE_CM


def grid_check_actors(actors, origin):
    report = {}
    for step in GRID_STEPS_CM:
        fit = 0
        misfits = []
        for actor in actors:
            box = bounds_of(actor)
            if not box:
                continue
            low, high = box
            name = mesh_name_of(actor)
            for axis, label in ((0, "x"), (1, "y")):
                relative_min = low[axis] - origin[axis]
                relative_max = high[axis] - origin[axis]
                extent = relative_max - relative_min
                if extent <= THIN_CM:
                    continue
                for kind, value in (("min", relative_min), ("max", relative_max), ("width", extent)):
                    if grid_fits(value, step):
                        fit += 1
                        continue
                    misfits.append({
                        "mesh": name,
                        "axis": label,
                        "kind": kind,
                        "value_cm": round(value, 1),
                        "residual_cm": round(grid_residual(value, step), 1),
                        "step_cm": step,
                    })
        names = sorted({row["mesh"] for row in misfits})
        report[str(int(step))] = {
            "checked": fit + len(misfits),
            "fit": fit,
            "misfit_count": len(misfits),
            "misfit_names": names,
            "misfits": misfits[:40],
        }
    return report


def catalog_modules(meshes):
    rows = []
    for mesh in meshes or []:
        size = mesh.get("size")
        if not size or len(size) < 3:
            continue
        widths = sorted({round(float(value), 1) for value in size if float(value) > THIN_CM})
        if not widths:
            continue
        name = mesh.get("name") or mesh_name_of(mesh)
        nominal = nominal_width_cm(str(name))
        # The name suffix is the module width. Without one, the longest side is the width
        # and a shorter side over 40 cm is often the storey height, not another module.
        module = float(nominal) if nominal is not None else (max(widths) if widths else None)
        rows.append({
            "name": name,
            "widths_cm": widths,
            "nominal_cm": nominal,
            "module_cm": None if module is None else round(module, 1),
            "module_fit_300": None if module is None else grid_fits(module, 300.0),
            "module_fit_50": None if module is None else grid_fits(module, 50.0),
            "fit_300": all(grid_fits(value, 300.0) for value in widths),
            "fit_50": all(grid_fits(value, 50.0) for value in widths),
        })
    rows.sort(key=lambda row: row["name"])
    return rows


def l_shape(slabs, footprint, floors):
    """True when the lowest slab floor leaves a corner of its own bounding box empty."""
    if not slabs or not footprint or not floors:
        return False
    lowest = floors[0]["index"]
    plates = []
    for actor in slabs:
        match = SLAB_FLOOR.search(mesh_name_of(actor))
        if match and int(match.group(1)) == lowest:
            plates.append(actor)
    if not plates:
        plates = slabs
    min_x, min_y, max_x, max_y = union_xy(plates) or footprint
    cell = 300.0
    count_x = max(1, int(round((max_x - min_x) / cell)))
    count_y = max(1, int(round((max_y - min_y) / cell)))
    if count_x < 2 or count_y < 2:
        return False
    occupied = []
    for iy in range(count_y):
        row = []
        for ix in range(count_x):
            x = min_x + (ix + 0.5) * (max_x - min_x) / count_x
            y = min_y + (iy + 0.5) * (max_y - min_y) / count_y
            row.append(any(
                bounds_of(plate)
                and bounds_of(plate)[0][0] <= x <= bounds_of(plate)[1][0]
                and bounds_of(plate)[0][1] <= y <= bounds_of(plate)[1][1]
                for plate in plates
            ))
        occupied.append(row)
    total = count_x * count_y
    filled = sum(cell_on for row in occupied for cell_on in row)
    if filled == 0 or filled / total > 0.82:
        return False
    corners = (
        occupied[0][0],
        occupied[0][-1],
        occupied[-1][0],
        occupied[-1][-1],
    )
    return any(not corner for corner in corners) and any(corners)


def series_guess(wall_counts, storeys, shaped_l, shop_count, flats_per_landing):
    brick = wall_counts.get("BrickR", 0) + wall_counts.get("BrickW", 0)
    panel = wall_counts.get("Panel", 0) + wall_counts.get("PanelTile", 0) + wall_counts.get("Facade", 0)
    plaster = wall_counts.get("Plaster", 0)
    planks = wall_counts.get("Planks", 0)
    family, _count = max(
        (("planks", planks), ("brick", brick), ("panel", panel), ("plaster", plaster)),
        key=lambda item: item[1],
    )
    if shaped_l and shop_count > 0:
        return "L-block with shops"
    if family == "planks" and planks > 0:
        return "barracks (planks)"
    if family == "plaster" and plaster > 0 and storeys >= 4:
        return "Stalin (plaster)"
    if family == "brick" and brick > 0:
        return "brick (1-447 guess)" if storeys >= 4 else "brick low-rise"
    if family == "panel" and panel > 0:
        if storeys == 5:
            return "panel 5-storey (1-464 / 1-335 guess)"
        if storeys >= 9:
            return "panel high-rise"
        return "panel"
    if flats_per_landing is not None and flats_per_landing >= 8:
        return "dormitory guess"
    return "unknown"


def building_identity(entry, index):
    explicit = entry.get("id")
    if explicit is not None and str(explicit).strip() != "":
        text = str(explicit).strip()
        if text.isdigit():
            return "%02d" % int(text)
        return text
    match = BUILDING_ID.search(str(entry.get("map") or ""))
    if match:
        return "%02d" % int(match.group(1))
    return "%02d" % (index + 1)


def point_row(name, low, high, footprint, labels):
    x, y, z = center(low, high)
    edge = nearest_edge(x, y, footprint)
    return {
        "mesh": name,
        "edge": edge,
        "side": labels[edge],
        "x": round(x, 1),
        "y": round(y, 1),
        "z": round(z, 1),
        "along": round(along_fraction(edge, x, y, footprint), 3),
    }


def analyse_building(entry, index):
    actors = list(entry.get("actors") or [])
    notes = list(entry.get("actor_errors") or [])
    if entry.get("error"):
        notes.append(str(entry["error"]))
    plates, footprint_source = footprint_records(actors)
    footprint = union_xy(plates)
    floors = slab_floors(actors)
    height_cm, gaps = storey_height_cm(floors)
    names = [mesh_name_of(actor) for actor in actors]

    balcony_edge_counts = {edge: 0 for edge in EDGES}
    balcony_points = []
    if footprint:
        for actor in actors:
            name = mesh_name_of(actor)
            if not is_balcony_volume(name):
                continue
            box = bounds_of(actor)
            if not box:
                continue
            x, y, _z = center(*box)
            edge = nearest_edge(x, y, footprint)
            balcony_edge_counts[edge] += 1
            balcony_points.append((name, edge))
        labels = classify_edges(footprint, balcony_edge_counts)
    else:
        labels = {edge: "unknown" for edge in EDGES}

    entrances = []
    stair_pieces = []
    windows = []
    flat_leaves = []
    flat_walls = []
    partitions = 0
    shop_windows = []
    shop_doors = []
    roof_meshes = []
    facade_counts = {}
    wall_counts = {family: 0 for family in WALL_FAMILIES}
    for actor in actors:
        name = mesh_name_of(actor)
        box = bounds_of(actor)
        family = wall_family(name)
        if family:
            wall_counts[family] += 1
        if "Dormer" in name or "Roof" in name or "Parapet" in name:
            if "Stair" not in name:
                roof_meshes.append(name)
        if is_shop_door(name):
            shop_doors.append(name)
        elif is_shop_window(name):
            shop_windows.append(name)
        if is_partition(name):
            partitions += 1
        if not box or not footprint:
            continue
        low, high = box
        if is_entrance(name):
            entrances.append(point_row(name, low, high, footprint, labels))
        if is_stairwell_piece(name):
            x, y, _z = center(low, high)
            stair_pieces.append({"name": name, "x": x, "y": y})
        if is_window(name):
            x, y, z = center(low, high)
            edge = nearest_edge(x, y, footprint)
            windows.append({
                "mesh": name,
                "type": window_type(name),
                "edge": edge,
                "side": labels[edge],
                "floor": nearest_floor(low[2], floors),
                "width_cm": round(facade_axis_width(low, high, edge), 1),
                "nominal_cm": nominal_width_cm(name),
                "along": round(along_fraction(edge, x, y, footprint), 3),
            })
        kind = facade_kind(name)
        if kind is not None:
            x, y, _z = center(low, high)
            edge = nearest_edge(x, y, footprint)
            placed_width = round(facade_axis_width(low, high, edge), 1)
            nominal = nominal_width_cm(name)
            key = (labels[edge], kind, nominal if nominal is not None else placed_width)
            facade_counts[key] = facade_counts.get(key, 0) + 1
        if is_flat_door_leaf(name):
            flat_leaves.append((name, low, high))
        elif is_flat_door_wall(name):
            flat_walls.append((name, low, high))

    use_leaves = bool(flat_leaves)
    flat_doors = flat_leaves if use_leaves else flat_walls
    stairwells = cluster_stairwells(stair_pieces)
    for well in stairwells:
        edge = nearest_edge(well["x"], well["y"], footprint) if footprint else None
        well["edge"] = edge
        well["side"] = labels.get(edge, "unknown")
        well["x"] = round(well["x"], 1)
        well["y"] = round(well["y"], 1)

    landing_counts = {}
    for _name, low, high in flat_doors:
        x, y, _z = center(low, high)
        floor = nearest_floor(low[2], floors)
        well = nearest_stairwell(x, y, stairwells)
        key = (well, floor)
        landing_counts[key] = landing_counts.get(key, 0) + 1
    landing_rows = [
        {"stairwell": well, "floor": floor, "flat_doors": count}
        for (well, floor), count in sorted(landing_counts.items(), key=lambda item: (item[0][0], item[0][1] or -1))
    ]
    flats_per_landing = as_count(statistics.median([row["flat_doors"] for row in landing_rows])) if landing_rows else None
    door_total = sum(row["flat_doors"] for row in landing_rows)
    rooms_per_flat = round(1.0 + partitions / door_total, 2) if door_total else None

    if any("Dormer" in name for name in names):
        roof_type = "pitched_dormer"
    elif any(("Roof" in name or "Parapet" in name) and "Stair" not in name for name in names):
        roof_type = "flat"
    else:
        roof_type = "unknown"

    shaped_l = l_shape(
        [actor for actor in actors if "Slab_F" in mesh_name_of(actor)],
        footprint,
        floors,
    )
    shop_count = len(shop_windows) + len(shop_doors)
    guess = series_guess(wall_counts, len(floors), shaped_l, shop_count, flats_per_landing)
    sections = len(stairwells) if stairwells else (1 if footprint else 0)

    windows_by_facade = {side: [] for side in ("street", "court", "end", "unknown")}
    type_counts = {side: {} for side in windows_by_facade}
    for window in windows:
        side = window["side"]
        windows_by_facade.setdefault(side, []).append(window)
        bucket = type_counts.setdefault(side, {})
        bucket[window["type"]] = bucket.get(window["type"], 0) + 1
    balconies_by_facade = {"street": 0, "court": 0, "end": 0}
    for _name, edge in balcony_points:
        side = labels.get(edge, "end")
        if side not in balconies_by_facade:
            balconies_by_facade[side] = 0
        balconies_by_facade[side] += 1

    origin = (footprint[0], footprint[1]) if footprint else (0.0, 0.0)
    plan = {
        "id": building_identity(entry, index),
        "map": entry.get("map"),
        "loaded": bool(entry.get("loaded", True)),
        "actor_count": len(actors),
        "footprint_source": footprint_source,
        "footprint_m": None,
        "storeys": len(floors),
        "slab_floors": [floor["index"] for floor in floors],
        "storey_height_m": None if height_cm is None else round(height_cm / 100.0, 2),
        "storey_heights_cm": gaps,
        "sections": sections,
        "entrances": entrances,
        "stairwells": stairwells,
        "flat_door_source": "Door_Apt" if use_leaves else ("IntWall_aptdoor" if flat_walls else None),
        "aptdoor_wall_modules": len(flat_walls),
        "flats_per_landing": flats_per_landing,
        "landings": landing_rows,
        "partition_modules": partitions,
        "rooms_per_flat": rooms_per_flat,
        "windows_by_facade": {
            side: rows for side, rows in windows_by_facade.items() if rows
        },
        "window_types_by_facade": {
            side: counts for side, counts in type_counts.items() if counts
        },
        "facade_modules": [
            {"side": side, "kind": kind, "width_cm": width, "count": count}
            for (side, kind, width), count in sorted(facade_counts.items())
        ],
        "balconies_by_facade": balconies_by_facade,
        "shop_windows": len(shop_windows),
        "shop_doors": len(shop_doors),
        "shop_window_meshes": sorted(set(shop_windows)),
        "shop_door_meshes": sorted(set(shop_doors)),
        "roof_type": roof_type,
        "roof_meshes": sorted(set(roof_meshes)),
        "wall_families": {family: count for family, count in wall_counts.items() if count},
        "series_guess": guess,
        "l_shape": shaped_l,
        "grid": grid_check_actors(actors, origin) if footprint else {"300": empty_grid(), "50": empty_grid()},
        "notes": notes[:20],
    }
    if footprint:
        min_x, min_y, max_x, max_y = footprint
        plan["footprint_m"] = {
            "width": round((max_x - min_x) / 100.0, 2),
            "depth": round((max_y - min_y) / 100.0, 2),
            "min_cm": [round(min_x, 1), round(min_y, 1)],
            "max_cm": [round(max_x, 1), round(max_y, 1)],
        }
    return plan


def empty_grid():
    return {"checked": 0, "fit": 0, "misfit_count": 0, "misfit_names": [], "misfits": []}


def summarise(plan):
    footprint = plan.get("footprint_m") or {}
    return {
        "id": plan["id"],
        "series_guess": plan["series_guess"],
        "footprint_m": [footprint.get("width"), footprint.get("depth")],
        "storeys": plan["storeys"],
        "storey_height_m": plan["storey_height_m"],
        "sections": plan["sections"],
        "flats_per_landing": plan["flats_per_landing"],
        "entrances": len(plan["entrances"]),
        "roof_type": plan["roof_type"],
        "l_shape": plan["l_shape"],
        "grid_300_misfit_meshes": len(plan["grid"]["300"]["misfit_names"]),
        "grid_50_misfit_meshes": len(plan["grid"]["50"]["misfit_names"]),
    }


def build_plans(dump, source=None):
    if not isinstance(dump, dict) or "buildings" not in dump:
        raise ValueError("kit dump must be an object with a buildings list")
    buildings = [analyse_building(entry, index) for index, entry in enumerate(dump.get("buildings") or [])]
    buildings.sort(key=lambda plan: plan["id"])
    return {
        "schema": "codex.kit_plans/1",
        "source": source,
        "units_in": dump.get("units", "cm"),
        "mesh_modules": catalog_modules(dump.get("meshes")),
        "buildings": buildings,
        "summary": [summarise(plan) for plan in buildings],
    }


def render_markdown(plans):
    lines = [
        "# Soviet kit floor plans",
        "",
        "Heuristic reading of `kit_buildings.json`. Street is the long side with more balcony volumes;",
        "court is the opposite long side. Module edges are measured from the slab footprint.",
        "A placed piece fits a grid when min, max and width sit within 2 cm of a multiple.",
        "Widths of 40 cm or less are treated as thickness and skipped.",
        "",
        "| id | series guess | footprint m | storeys | height m | sections | flats / landing | entrances | roof |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in plans["summary"]:
        width, depth = row["footprint_m"]
        size = "" if width is None else "%s x %s" % (width, depth)
        lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            row["id"], row["series_guess"], size, row["storeys"], row["storey_height_m"],
            row["sections"], row["flats_per_landing"], row["entrances"], row["roof_type"],
        ))
    lines.append("")
    catalog = plans.get("mesh_modules") or []
    module_300 = sum(1 for row in catalog if row.get("module_fit_300"))
    module_50 = sum(1 for row in catalog if row.get("module_fit_50"))
    box_300 = sum(1 for row in catalog if row.get("fit_300"))
    lines.append(
        "Mesh catalog: %d modules with a side over 40 cm. "
        "Module width (name suffix, else the longest side) on 300 cm: %d. On 50 cm: %d. "
        "Whole box on 300 cm: %d." % (len(catalog), module_300, module_50, box_300))
    lines.append("")
    for plan in plans["buildings"]:
        footprint = plan.get("footprint_m") or {}
        lines.append("## Building %s" % plan["id"])
        lines.append("")
        lines.append("- Map: `%s`" % plan.get("map"))
        lines.append("- Footprint: %s x %s m (%s), storeys %s, storey height %s m, sections %s." % (
            footprint.get("width"), footprint.get("depth"), plan["footprint_source"],
            plan["storeys"], plan["storey_height_m"], plan["sections"]))
        lines.append("- Series guess: %s. L-shape: %s. Roof: %s (%s)." % (
            plan["series_guess"], plan["l_shape"], plan["roof_type"], ", ".join(plan["roof_meshes"]) or "none"))
        lines.append("- Flats per landing: %s (from %s). Rooms per flat estimate: %s from %s partition modules." % (
            plan["flats_per_landing"], plan["flat_door_source"], plan["rooms_per_flat"], plan["partition_modules"]))
        lines.append("- Shop window modules: %s. Shop door modules: %s. Balconies: %s." % (
            plan["shop_windows"], plan["shop_doors"], plan["balconies_by_facade"]))
        if plan["entrances"]:
            bits = ["%s on %s (%s)" % (row["mesh"], row["side"], row["edge"]) for row in plan["entrances"]]
            lines.append("- Entrances: %s." % "; ".join(bits))
        else:
            lines.append("- Entrances: none.")
        if plan["stairwells"]:
            bits = ["#%s on %s (%s), %s pieces" % (row["index"], row["side"], row["edge"], row["members"])
                    for row in plan["stairwells"]]
            lines.append("- Stairwells: %s." % "; ".join(bits))
        else:
            lines.append("- Stairwells: none.")
        if plan["landings"]:
            bits = ["stair %s floor %s: %s" % (row["stairwell"], row["floor"], row["flat_doors"])
                    for row in plan["landings"]]
            lines.append("- Landings: %s." % "; ".join(bits))
        for side, counts in plan["window_types_by_facade"].items():
            listed = ", ".join("%s x%s" % (name, count) for name, count in sorted(counts.items()))
            lines.append("- Windows on %s: %s." % (side, listed))
        if plan["facade_modules"]:
            listed = ", ".join("%s %s %s cm x%s" % (row["side"], row["kind"], row["width_cm"], row["count"])
                               for row in plan["facade_modules"])
            lines.append("- Facade modules: %s." % listed)
        for step in ("300", "50"):
            grid = plan["grid"][step]
            lines.append("- Grid %s cm: %s/%s checks fit, %s meshes miss (%s)." % (
                step, grid["fit"], grid["checked"], len(grid["misfit_names"]),
                ", ".join(grid["misfit_names"][:12]) or "none"))
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def write_plans(plans, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    json_path = os.path.join(out_dir, "kit_plans.json")
    md_path = os.path.join(out_dir, "kit_plans.md")
    with open(json_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(plans, handle, indent=2)
        handle.write("\n")
    with open(md_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(render_markdown(plans))
    return json_path, md_path


def main(argv=None):
    parser = argparse.ArgumentParser(description="Read a Soviet kit dump and write floor-plan reports.")
    parser.add_argument("--input", default=None, help="kit_buildings.json from dump_kit_buildings.py")
    parser.add_argument("--out-dir", default=None, help="Directory for kit_plans.json and kit_plans.md")
    args = parser.parse_args(argv)
    source = os.path.abspath(args.input or default_input_path())
    if not os.path.isfile(source):
        raise SystemExit("kit dump not found: %s" % source)
    with open(source, encoding="utf-8") as handle:
        dump = json.load(handle)
    out_dir = os.path.abspath(args.out_dir or os.path.dirname(source))
    plans = build_plans(dump, source=source)
    json_path, md_path = write_plans(plans, out_dir)
    print("kit plans: %d buildings -> %s" % (len(plans["buildings"]), json_path))
    print("kit plans markdown -> %s" % md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
