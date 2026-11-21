"""KIT DRESS (docs/design/levelgen/KIT_DRESS.md): every generated building that has a facade series (Content/Data/LevelGen/kit_series.json,
a building's `series` field or its archetype's default) dressed with the Soviet building kit (Content/SovietHouses, untracked - never
committed) on a NEW map under /Game/Maps/KitPilot/. Generalises the one-building pilot (kit_pilot_building.py, KIT_PILOT_2026-11-20.md).

The generator and Scripts/Editor/build_levelgen_map.py are not changed: the builder is loaded as a library (its main() is not run), plans
the accepted layout, and kit_dress_lib.Dresser reads each building's own specs + layout records generically (openings, partitions, rooms,
stairs, tags) - so it follows whatever plans the generator produced. The builder pieces a kit visual replaces stay as HIDDEN proxies
(hidden in game + invisible; collision, navigation and tags unchanged: cover, sight lines, mantle edges, door states and the walking
ramps are the generator's, bit for bit); anything not replaced stays visible grey box. Kit visuals: ONE actor per building
(StaticMeshActor "Kit_<id>") holding one InstancedStaticMeshComponent per mesh / material / mirror / group (group "roof" carries the
component tag kit_roof), NoCollision, CanEverAffectNavigation false.

Modes (env KIT_DRESS_MODE, or the first argument under system python):
  build (default, editor commandlet):
      UnrealEditor-Cmd.exe <uproject> -run=pythonscript "-script=<this file>" -unattended -nullrhi
      -> the map, Content/Data/LevelJson/<map>.json (levelgen_layout = the layout used; environment locked Clear 13:00, no snow cover),
         Saved/KitBlocks/kit_dress_report_<map>.json, Saved/Logs/KitDressLayout[_<map>].txt. Then: Scripts/bake_navmesh.ps1 -Map <map>
  shots (in game, rendered; `py <file>` from -ExecCmds cannot take a path with spaces: copy this file to C:\\Temp first):
      UnrealEditor.exe <uproject> <map> -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound -unattended -ExecCmds="py C:\\Temp\\kit_dress.py"
      (KIT_DRESS_MODE=shots, KIT_DRESS_TARGET=<map>) -> HighResShot pictures copied to Saved/KitBlocks/shots/ + kit_dress_shots.txt (proxy
      checks, kit actor / instance counts in the running game).
  plan (system python): `python kit_dress.py plan [layout.json] [builder root]` -> counts, Saved/KitBlocks/kit_dress_plan.json.
  export (system python): `python kit_dress.py export <git ref> [layout name]` -> the builder (+ its data and the accepted layout) of
      another branch under Saved/KitBlocks/builder/<ref>/ (git show, read only): dress a layout whose builder is newer than this checkout
      with KIT_DRESS_BUILDER=<that dir> KIT_DRESS_LAYOUT=<that dir>/Content/Data/LevelGen/accepted/<layout>.json.
Env: KIT_DRESS_LAYOUT (accepted layout), KIT_DRESS_BUILDER (builder tree root, default this checkout), KIT_DRESS_TARGET (map, must be
under /Game/Maps/KitPilot/, default /Game/Maps/KitPilot/L_KitBlocks), KIT_DRESS_BUILDINGS (comma list, default every building with a
series), KIT_DRESS_SERIES (series table), KIT_DRESS_DRESS=0 (a grey copy: the builder's map only - the smoke baseline), KIT_DRESS_FILLER
(overlap | squish), KIT_DRESS_SHOT_ONLY / KIT_DRESS_SHOT_PREFIX. Never touches another map. Godot reference: none (UE-only).
"""
import datetime
import json
import math
import os
import shutil
import subprocess
import sys
import types

try:
    import unreal
except ImportError:  # system python: plan / export
    unreal = None


def _project_dir():
    if unreal is not None:
        return os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


PROJECT_DIR = _project_dir()
EDITOR_DIR = os.path.join(PROJECT_DIR, "Scripts", "Editor")
if EDITOR_DIR not in sys.path:
    sys.path.insert(0, EDITOR_DIR)
import kit_dress_lib as K  # noqa: E402

MODE = os.environ.get("KIT_DRESS_MODE", "build")
DEFAULT_LAYOUT = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_bgen_plans_check.json")
LAYOUT_FILE = os.path.abspath(os.environ.get("KIT_DRESS_LAYOUT") or DEFAULT_LAYOUT)
BUILDER_ROOT = os.path.abspath(os.environ.get("KIT_DRESS_BUILDER") or PROJECT_DIR)
TARGET = os.environ.get("KIT_DRESS_TARGET", "/Game/Maps/KitPilot/L_KitBlocks")
ONLY = [s for s in os.environ.get("KIT_DRESS_BUILDINGS", "").split(",") if s]
SERIES_FILE = os.environ.get("KIT_DRESS_SERIES") or os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "kit_series.json")
DRESS = os.environ.get("KIT_DRESS_DRESS", "1") != "0"
FILLER_MODE = os.environ.get("KIT_DRESS_FILLER", "overlap")
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "KitBlocks")
SHOTS_DIR = os.environ.get("KIT_DRESS_SHOTS_DIR") or os.path.join(OUT_DIR, "shots")
# "story" = the story showcase views (story_cameras: overview, every building's front 3/4, each story element's close-up) instead of the
# kit block views (cameras)
SHOT_SET = os.environ.get("KIT_DRESS_SHOT_SET", "kit")
SHOT_PREFIX = os.environ.get("KIT_DRESS_SHOT_PREFIX", "kitblocks_")
SHOT_ONLY = [s for s in os.environ.get("KIT_DRESS_SHOT_ONLY", "").split(",") if s]
CAM_TAG = "KitDressCam"
KIT_TAG = "KitDress"
PROXY_TAG = "KitDressProxy"
BUILDER_FILES = ["Scripts/Editor/build_levelgen_map.py", "Scripts/Editor/levelgen_ladders.py", "Scripts/Editor/levelgen_bot_route.py",
                 "Content/Data/LevelGen/building_coord_fields.json", "Content/Data/LevelGen/levelgen_v2.json"]

REPORT = []


def report(line):
    REPORT.append(str(line))
    if unreal is None:
        print(line, file=sys.stderr)


def rel_to_project(path):
    return os.path.relpath(os.path.abspath(path), PROJECT_DIR).replace("\\", "/")


# ---- the builder as a library (its main() is not run) --------------------------------------------------------------------------

def load_builder(root=None):
    root = root or BUILDER_ROOT
    path = os.path.join(root, "Scripts", "Editor", "build_levelgen_map.py")
    with open(path, encoding="utf-8") as f:
        src = f.read()
    tail = '\nif unreal is not None or __name__ == "__main__":\n    main()'
    if tail not in src:
        raise RuntimeError("%s: its main() guard changed - update kit_dress.load_builder" % path)
    os.environ["CODEX_LEVELGEN_LAYOUT"] = LAYOUT_FILE
    os.environ["CODEX_LEVELGEN_TARGET"] = TARGET
    d = os.path.dirname(path)
    if d in sys.path:
        sys.path.remove(d)
    sys.path.insert(0, d)
    for name in ("levelgen_ladders", "levelgen_bot_route"):   # the builder's own copies (an exported branch may differ)
        sys.modules.pop(name, None)
    mod = types.ModuleType("build_levelgen_map_kitdress")
    mod.__file__ = path
    exec(compile(src.replace(tail, "\n"), path, "exec"), mod.__dict__)
    mod.LEVELJSON_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelJson")   # the level JSON goes to THIS checkout
    return mod


def load_table():
    with open(SERIES_FILE, encoding="utf-8") as f:
        return json.load(f)


def dressable(b, table):
    name, S = K.series_for(b, table)
    if S is None:
        return None, None, "no series for archetype %r" % b.get("archetype")
    if ONLY and b["id"] not in ONLY:
        return None, None, "not in KIT_DRESS_BUILDINGS"
    if not (b.get("enterable") or b.get("openings") or b.get("partitions")):
        return None, None, "not enterable (one block)"
    if b.get("set_piece") == "chimney":
        return None, None, "chimney"
    return name, S, None


def own_specs(specs, bid):
    return [sp for sp in specs if str(sp.get("folder", "")) == "Buildings/%s" % bid or str(sp.get("folder", "")).startswith("Buildings/%s/" % bid)]


def plan_all():
    """-> (B, layout, specs with kit_hidden marks, {bid: dressing record}, table)."""
    B = load_builder()
    layout = B.load_layout(LAYOUT_FILE)
    specs = B.plan(layout)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    table = load_table()
    dressed = {}
    hide = {}
    if DRESS:
        for b in layout["buildings"]:
            name, S, why = dressable(b, table)
            if S is None:
                report("%s (%s): grey box - %s" % (b["id"], b.get("archetype"), why))
                continue
            deg = B.building_rotation(b)
            if deg:
                local, c, deg = B._building_local(layout, b)
                lb = local["buildings"][0]
            else:
                local, lb, c = layout, b, (0.0, 0.0)
            mine = [K.spec_to_local(sp, c, deg) for sp in own_specs(specs, b["id"])]
            state_of = lambda op, _l=local, _b=lb: B.effective_opening_state(_l, _b, op)  # noqa: E731
            D = K.Dresser(local, lb, mine, name, S, table, state_of, B.balcony_door_openings(lb), FILLER_MODE).dress()
            REPORT.extend(D.notes)
            x0, y0, x1, y1 = K.footprint_rect(b)
            dressed[b["id"]] = {"series": name, "archetype": b.get("archetype"), "instances": K.globalize(D.out, c, deg),
                                "centre": [(x0 + x1) / 2.0, (y0 + y1) / 2.0, float(layout.get("ground_z", 0.0))],
                                "parts": dict(sorted(D.counts.items())), "stretch": D.stats(), "notes": D.notes, "consumed": sorted(D.consumed),
                                "own_specs": len(mine)}
            hide[b["id"]] = D.consumed
            report("%s (%s, series %s): %d kit instances, %d of %d builder pieces become hidden proxies" % (
                b["id"], b.get("archetype"), name, len(D.out), len(D.consumed), len(mine)))
    out = []
    for sp in specs:
        folder = str(sp.get("folder", ""))
        bid = folder.split("/")[1] if folder.startswith("Buildings/") and "/" in folder else None
        if bid in hide and sp.get("label") in hide[bid]:
            sp = dict(sp, kit_hidden=True)
        out.append(sp)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    return B, layout, out, dressed, table


def summary(specs, dressed):
    per = {}
    for bid, d in dressed.items():
        groups = {(i["mesh"], i.get("mat"), i["scale"][0] < 0, i["group"]) for i in d["instances"]}
        per[bid] = {"series": d["series"], "archetype": d["archetype"], "kit_actors": 1, "ism_components": len(groups), "instances": len(d["instances"]),
                    "hidden_proxies": len(d["consumed"]), "builder_pieces": d["own_specs"], "visible_builder_pieces": d["own_specs"] - len(d["consumed"]),
                    "parts": d["parts"], "stretch": d["stretch"], "notes": d["notes"]}
    return {"map": TARGET, "layout": rel_to_project(LAYOUT_FILE), "builder": rel_to_project(BUILDER_ROOT) or ".", "series_file": rel_to_project(SERIES_FILE),
            "dressed": DRESS, "time": datetime.datetime.now().isoformat(), "builder_specs": len(specs),
            "hidden_proxies": sum(1 for s in specs if s.get("kit_hidden")),
            "totals": {"kit_actors": len(per), "ism_components": sum(p["ism_components"] for p in per.values()), "instances": sum(p["instances"] for p in per.values())},
            "buildings": per}


def write_report(data, name=None):
    os.makedirs(OUT_DIR, exist_ok=True)
    name = name or "kit_dress_report_%s.json" % TARGET.rsplit("/", 1)[-1]
    data = dict(data, report=REPORT)
    with open(os.path.join(OUT_DIR, name), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    return data


# ---- export another branch's builder (git show, read only) -----------------------------------------------------------------------

def export(ref, layout_name=None):
    safe = ref.replace("/", "_").replace("\\", "_").replace(":", "_")
    root = os.path.join(OUT_DIR, "builder", safe)
    files = list(BUILDER_FILES)
    if layout_name:
        files.append("Content/Data/LevelGen/accepted/%s.json" % layout_name)
    for rel in files:
        dst = os.path.join(root, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        data = subprocess.run(["git", "-C", PROJECT_DIR, "show", "%s:%s" % (ref, rel)], check=True, capture_output=True).stdout
        with open(dst, "wb") as f:
            f.write(data)
        print("%s -> %s (%d bytes)" % (rel, dst, len(data)))
    sha = subprocess.run(["git", "-C", PROJECT_DIR, "rev-parse", ref], check=True, capture_output=True, text=True).stdout.strip()
    with open(os.path.join(root, "EXPORTED_FROM.txt"), "w", encoding="utf-8") as f:
        f.write("%s %s %s\n" % (ref, sha, datetime.datetime.now().isoformat()))
    print("KIT_DRESS_BUILDER=%s" % root)
    if layout_name:
        print("KIT_DRESS_LAYOUT=%s" % os.path.join(root, "Content", "Data", "LevelGen", "accepted", layout_name + ".json"))


# ---- Unreal: the map -------------------------------------------------------------------------------------------------------------

class KitPlacer:
    def __init__(self, B, table):
        self.B = B
        self.table = table
        self.inner = B.Placer()
        self.actors = self.inner.actors
        self.meshes = {}
        self.materials = None
        self.used_mats = set()
        self.stats = {"kit_actors": 0, "ism_components": 0, "instances": 0, "hidden": 0, "fallback_actors": 0, "missing_meshes": 0}
        last = {}
        orig = self.inner.spawn

        def spawn(cls, spec, tags=None):
            a = orig(cls, spec, tags)
            last["a"] = a
            return a
        self.inner.spawn = spawn
        self.last = last

    def mesh(self, path):
        if path not in self.meshes:
            m = unreal.load_asset(path)
            if m is None:
                report("ERROR: kit mesh %s missing" % path)
                self.stats["missing_meshes"] += 1
            self.meshes[path] = m
        return self.meshes[path]

    def material(self, name):
        if not name:
            return None
        if self.materials is None:
            self.materials = {}
            for p in unreal.EditorAssetLibrary.list_assets(self.table.get("materials_root", "/Game/SovietHouses/Materials"), recursive=True, include_folder=False):
                self.materials[p.rsplit("/", 1)[-1].split(".")[0]] = p
        p = self.materials.get(name)
        if p is None:
            report("REVIEW: kit material %s missing" % name)
        return unreal.load_asset(p) if p else None

    @staticmethod
    def visual_only(comp):
        comp.set_collision_profile_name("NoCollision")
        try:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:
            pass
        if hasattr(comp, "set_can_ever_affect_navigation"):
            comp.set_can_ever_affect_navigation(False)
        else:
            comp.set_editor_property("can_ever_affect_navigation", False)
        comp.set_editor_property("generate_overlap_events", False)

    def place_proxy(self, spec):
        self.last.pop("a", None)
        self.inner.place(spec)
        a = self.last.get("a")
        if spec.get("kit_hidden") and a is not None:
            a.set_actor_hidden_in_game(True)
            a.static_mesh_component.set_editor_property("visible", False)   # invisible in the editor too; collision + navigation unchanged
            a.set_editor_property("tags", list(a.tags) + [unreal.Name(PROXY_TAG)])
            self.stats["hidden"] += 1

    def add_ism(self, actor, name):
        sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib = unreal.SubobjectDataBlueprintFunctionLibrary
        handles = sds.k2_gather_subobject_data_for_instance(actor)
        parent = handles[0]
        for h in handles:
            if lib.get_object(lib.get_data(h)) == actor.root_component:
                parent = h
                break
        params = unreal.AddNewSubobjectParams(parent_handle=parent, new_class=unreal.InstancedStaticMeshComponent, blueprint_context=None)
        res = sds.add_new_subobject(params)
        handle = res[0] if isinstance(res, (tuple, list)) else res
        try:
            sds.rename_subobject(handle, unreal.Text(name))
        except Exception:
            pass
        comp = lib.get_object(lib.get_data(handle))
        if comp is None:
            raise RuntimeError("SubobjectDataSubsystem gave no component (%r)" % (res,))
        return comp

    def place_building(self, bid, d):
        groups = {}
        for i in d["instances"]:
            groups.setdefault((i["mesh"], i.get("mat"), i["scale"][0] < 0, i["group"]), []).append(i)
        c = d["centre"]
        a = self.actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*c), unreal.Rotator(pitch=0.0, yaw=0.0, roll=0.0))
        a.set_actor_label("Kit_%s" % bid)
        a.set_folder_path("%s/Kit/%s" % (self.B.FOLDER, bid))
        a.set_editor_property("tags", [unreal.Name(self.B.TAG), unreal.Name(KIT_TAG), unreal.Name("kit_building=%s" % bid), unreal.Name("kit_series=%s" % d["series"])])
        root = a.static_mesh_component
        root.set_mobility(unreal.ComponentMobility.STATIC)
        self.visual_only(root)
        self.stats["kit_actors"] += 1
        for n, ((mp, mat, mirrored, group), insts) in enumerate(sorted(groups.items(), key=lambda kv: (kv[0][0], str(kv[0][1]), kv[0][2], kv[0][3]))):
            mesh = self.mesh(mp)
            if mesh is None:
                continue
            mi = self.material(mat) if mat else None
            if mat:
                self.used_mats.add(mat)
            name = "ISM_%02d_%s%s%s%s" % (n, mp.rsplit("/", 1)[-1].replace("SM_SH_", "").replace(".", "_"), "_" + mat.replace("MI_SH_", "") if mat else "",
                                           "_mirror" if mirrored else "", "_roof" if group == "roof" else "")
            try:
                comp = self.add_ism(a, name)
            except Exception as exc:
                report("REVIEW: %s: ISM component not added (%r) - %d single actors instead" % (bid, exc, len(insts)))
                self.fallback(bid, insts, mesh, mi)
                continue
            comp.set_static_mesh(mesh)
            if mi is not None:
                comp.set_material(0, mi)
            comp.set_mobility(unreal.ComponentMobility.STATIC)
            self.visual_only(comp)
            tags = ["kit_part=" + insts[0]["part"]] + (["kit_roof"] if group == "roof" else [])
            comp.set_editor_property("component_tags", [unreal.Name(t) for t in tags])
            xf = [unreal.Transform(unreal.Vector(*i["loc"]), unreal.Rotator(pitch=i["rot"][0], yaw=i["rot"][1], roll=i["rot"][2]), unreal.Vector(*i["scale"])) for i in insts]
            try:
                comp.add_instances(xf, False, True)
            except Exception:
                for t in xf:
                    comp.add_instance(t, True)
            got = comp.get_instance_count()
            if got != len(insts):
                report("REVIEW: %s %s: %d of %d instances" % (bid, name, got, len(insts)))
            self.stats["ism_components"] += 1
            self.stats["instances"] += got

    def ensure_ism_usage(self):
        """The base materials of every instanced mesh / override need "Used with Instanced Static Meshes": without it the game draws the
        default (checker) material on the instances (seen 2026-11-21: the pack's M_SH_Master lacks it). Sets the flag and saves those
        material packages - a local change of the untracked pack, once per checkout."""
        mats = set()
        for m in self.meshes.values():
            if m is None:
                continue
            for sm in m.get_editor_property("static_materials"):
                mats.add(sm.get_editor_property("material_interface"))
        for name in list(self.used_mats):
            mats.add(self.material(name))
        bases = {}
        for mi in mats:
            if mi is None:
                continue
            base = mi.get_base_material() if hasattr(mi, "get_base_material") else mi
            if base is not None:
                bases[base.get_path_name()] = base
        fixed = []
        root = self.table.get("materials_root", "/Game/SovietHouses/Materials").rstrip("/") + "/"
        for path, base in sorted(bases.items()):
            if not path.startswith(root):
                continue   # only the pack's own materials - never /Engine (2026-11-21: WorldGridMaterial was saved by mistake)
            if not base.get_editor_property("used_with_instanced_static_meshes"):
                base.set_editor_property("used_with_instanced_static_meshes", True)
                try:
                    unreal.MaterialEditingLibrary.recompile_material(base)
                except Exception:
                    pass
                unreal.EditorAssetLibrary.save_loaded_asset(base, False)
                fixed.append(path)
        report("ISM material usage: %d base materials (only those under the pack are touched), set 'Used with Instanced Static Meshes' on %d: %s" % (len(bases), len(fixed), ", ".join(fixed) or "-"))

    def fallback(self, bid, insts, mesh, mi):
        for i in insts:
            a = self.actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*i["loc"]), unreal.Rotator(pitch=i["rot"][0], yaw=i["rot"][1], roll=i["rot"][2]))
            a.set_folder_path("%s/Kit/%s/Single" % (self.B.FOLDER, bid))
            a.set_actor_scale3d(unreal.Vector(*i["scale"]))
            a.set_editor_property("tags", [unreal.Name(self.B.TAG), unreal.Name(KIT_TAG)] + ([unreal.Name("kit_roof")] if i["group"] == "roof" else []))
            a.static_mesh_component.set_static_mesh(mesh)
            if mi is not None:
                a.static_mesh_component.set_material(0, mi)
            self.visual_only(a.static_mesh_component)
            self.stats["fallback_actors"] += 1


def build():
    if not TARGET.startswith("/Game/Maps/KitPilot/"):
        raise ValueError("KIT_DRESS_TARGET must be under /Game/Maps/KitPilot/ (got %s)" % TARGET)
    B, layout, specs, dressed, table = plan_all()
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(TARGET):
        report("deleting the previous kit map %s -> %s" % (TARGET, lib.delete_asset(TARGET)))
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    B.ensure_lighting(actors)
    placer = KitPlacer(B, table)
    for sp in specs:
        placer.place_proxy(sp)
    for bid, d in sorted(dressed.items()):
        placer.place_building(bid, d)
    placer.ensure_ism_usage()
    # the shots' camera
    cam = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0.0, 0.0, 3000.0), unreal.Rotator(pitch=-45.0, yaw=0.0, roll=0.0))
    cam.set_actor_label("KitDress_Camera")
    cam.set_folder_path(B.FOLDER + "/Kit/Camera")
    cam.set_editor_property("tags", [unreal.Name(B.TAG), unreal.Name(CAM_TAG)])
    cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    nav = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in nav:
        actors.destroy_actor(a)
    total = len(actors.get_all_level_actors())
    report("placed %d builder actors (%d hidden proxies), %d kit actors with %d ISM components / %d instances, %d fallback single actors; %d actors in the level; destroyed %d RecastNavMesh" % (
        placer.inner.placed, placer.stats["hidden"], placer.stats["kit_actors"], placer.stats["ism_components"], placer.stats["instances"],
        placer.stats["fallback_actors"], total, len(nav)))
    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, TARGET)
    report("saved %s: %s" % (TARGET, saved))
    path = B.write_level_json(layout, TARGET)
    with open(path, encoding="utf-8") as f:
        lj = json.load(f)
    lj["levelgen_layout"] = rel_to_project(LAYOUT_FILE)
    lj["level_name"] = "Kit blocks (%s)" % os.path.basename(LAYOUT_FILE)
    lj["description"] = ("Kit dressing showcase (Scripts/Editor/kit_dress.py, docs/design/levelgen/KIT_DRESS.md): every building with a facade series "
                         "dressed with the Soviet kit; the generator's grey boxes stay as hidden collision / navigation proxies.") if DRESS else \
                        "Grey copy of the kit dressing showcase layout (Scripts/Editor/kit_dress.py KIT_DRESS_DRESS=0): the smoke baseline."
    # no snow cover: the snow layer also lands on the interior floors (kit pilot 2026-11-20)
    lj["environment"] = {"_comment": "kit dressing pictures: locked Clear 13:00, no snow cover", "locked": True, "start_hour": 13, "weather": "Clear", "snow_cover": 0.0}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(lj, f, indent=2)
        f.write("\n")
    REPORT.extend("builder: " + line for line in B.REPORT)
    data = summary(specs, dressed)
    data.update({"saved": bool(saved), "level_json": rel_to_project(path), "placed": dict(placer.stats, builder_actors=placer.inner.placed, level_actors=total)})
    write_report(data)


# ---- Unreal: rendered pictures in game ----------------------------------------------------------------------------------------------

def look(loc, at):
    dx, dy, dz = at[0] - loc[0], at[1] - loc[1], at[2] - loc[2]
    return math.degrees(math.atan2(dz, math.hypot(dx, dy))), math.degrees(math.atan2(dy, dx))


def vadd(*vs):
    return tuple(sum(c) for c in zip(*vs))


def vmul(v, k):
    return tuple(c * k for c in v)


def showcase_block(layout, table):
    """The dressed building the close views use: the block with the longest side (ties: the most storeys)."""
    cands = [b for b in layout["buildings"] if dressable(b, table)[1] is not None]
    blocks = [b for b in cands if b.get("archetype") == "block"] or cands
    if not blocks:
        return None

    def key(b):
        x0, y0, x1, y1 = K.footprint_rect(b)
        return (max(x1 - x0, y1 - y0), int(b["floors"]))
    return max(blocks, key=key)


def street_edge_of(layout, b, edges):
    sw = (b.get("plan") or {}).get("street_wall")
    if sw:
        w = next((w for w in layout["walls"] if str(w.get("id", "")).startswith(sw)), None)
        if w:
            e = K.edge_of_point(edges, ((w["a"][0] + w["b"][0]) / 2.0, (w["a"][1] + w["b"][1]) / 2.0))
            if e:
                return e
    return max(edges, key=lambda e: e.L)


CAM_NORMALS = {}   # shot name -> the outward normal of the facade it looks at (a shaded facade gets more exposure)


def cameras(layout, table):
    """The designer's views: (name, location, pitch, yaw, fov, hide roofs); CAM_NORMALS gets the facade views' normals."""
    gz = float(layout.get("ground_z", 0.0))
    (bx0, by0), (bx1, by1) = layout["bounds"]["min"], layout["bounds"]["max"]
    cams = []
    # the whole map, 3/4 from the south-west and straight down
    cx, cy = (bx0 + bx1) / 2.0, (by0 + by1) / 2.0
    span = max(bx1 - bx0, by1 - by0)
    loc = (bx0 - 0.35 * span, cy - 0.55 * span, gz + 0.55 * span)
    p, y = look(loc, (cx, cy, gz))
    cams.append(("overview_34", loc, p, y, 55.0, False))
    h = (by1 - by0 + 1500.0) / (2.0 * math.tan(math.radians(25.0))) + gz + 1000.0
    cams.append(("map_top_down", (cx, cy, h), -89.9, 0.0, 50.0, False))
    b = showcase_block(layout, table)
    if b is None:
        return cams
    edges = K.edges_of(b)
    floors = int(b["floors"])
    H = 300.0 * floors
    street = street_edge_of(layout, b, edges)
    court = next(e for e in edges if e.n == (-street.n[0], -street.n[1]))
    ops = b.get("openings", []) or []

    def facade_view(name, e, side):
        c = e.at(e.L / 2.0)
        loc = vadd(c, vmul(e.n, 0.75 * e.L), vmul(e.d, side * 0.55 * e.L)) + (gz + 900.0,)
        pitch, yaw = look(loc, c + (gz + H * 0.45,))
        cams.append((name, loc, pitch, yaw, 50.0, False))
        CAM_NORMALS[name] = e.n

    facade_view("street_facade_34", street, -1.0)
    entries = {e.name: sum(1 for o in ops if o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and K.edge_of_point(edges, o["pos"]) is e) for e in edges}
    ent_edge = court if entries.get(court.name, 0) >= entries.get(street.name, 0) else street
    facade_view("court_entrances_34", ent_edge, 1.0)
    doors = [o for o in ops if o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and K.edge_of_point(edges, o["pos"]) is ent_edge]
    if doors:
        p = doors[0]["pos"]
        loc = vadd(tuple(p), vmul(ent_edge.n, 650.0), vmul(ent_edge.d, -260.0)) + (gz + 230.0,)
        pitch, yaw = look(loc, tuple(p) + (gz + 160.0,))
        cams.append(("entrance_closeup", loc, pitch, yaw, 55.0, False))
        CAM_NORMALS["entrance_closeup"] = ent_edge.n
    wins = [o for o in ops if o.get("kind") == "window" and int(o.get("floor", 0)) == min(1, floors - 1) and K.edge_of_point(edges, o["pos"]) is street]
    if wins:
        p = min(wins, key=lambda o: street.s_of(o["pos"]))["pos"]
        zc = gz + 300.0 * min(1, floors - 1) + 150.0
        loc = vadd(tuple(p), vmul(street.n, 650.0), vmul(street.d, 260.0)) + (zc + 20.0,)
        pitch, yaw = look(loc, vadd(tuple(p), vmul(street.d, -90.0)) + (zc - 40.0,))
        cams.append(("window_closeup", loc, pitch, yaw, 50.0, False))
        CAM_NORMALS["window_closeup"] = street.n
    srooms = [r for r in b.get("rooms", []) or [] if r.get("kind") == "stairwell" and int(r["floor"]) == 0]
    if srooms:
        r = srooms[0]
        (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
        sc = ((rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0)
        st = next((s for s in b.get("stairs", []) or [] if s.get("kind") == "stairwell" and int(s["from"]) == 0
                   and rx0 - 1 <= s["pos"][0] <= rx1 + 1 and ry0 - 1 <= s["pos"][1] <= ry1 + 1), None)
        if st:
            far = st.get("turn") or st["top"]
            v = K.unit((far[0] - st["pos"][0], far[1] - st["pos"][1]))
            # from the floor landing end, eye height, looking along the first flight
            back = (st["pos"][0] - v[0] * 120.0, st["pos"][1] - v[1] * 120.0)
            loc = back + (gz + 175.0,)
            pitch, yaw = look(loc, (far[0], far[1], gz + 140.0))
            cams.append(("stairwell_interior", loc, pitch, yaw, 85.0, False))
        loc = (sc[0] + 200.0, sc[1] - 250.0, gz + H + 700.0)
        pitch, yaw = look(loc, (sc[0], sc[1], gz + 300.0))
        cams.append(("stairwell_above_roof_off", loc, pitch, yaw, 60.0, True))
    for r in b.get("rooms", []) or []:
        if r.get("kind") != "room" or int(r["floor"]) != 0:
            continue
        (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
        ws = [o for o in ops if o.get("kind") == "window" and int(o.get("floor", 0)) == 0 and rx0 - 16 <= o["pos"][0] <= rx1 + 16 and ry0 - 16 <= o["pos"][1] <= ry1 + 16]
        if not ws:
            continue
        p = ws[0]["pos"]
        cx_, cy_ = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
        loc = (cx_ + (cx_ - p[0]) * 0.75, cy_ + (cy_ - p[1]) * 0.75, gz + 170.0)
        pitch, yaw = look(loc, (p[0], p[1], gz + 120.0))
        cams.append(("flat_interior", loc, pitch, yaw, 85.0, False))
        break
    x0, y0, x1, y1 = K.footprint_rect(b)
    long_y = (y1 - y0) >= (x1 - x0)
    span = max(y1 - y0, x1 - x0) + 900.0
    h = span / (2.0 * math.tan(math.radians(20.0))) + gz + H
    yaw = 0.0 if long_y else 90.0
    cams.append(("top_down_roof_off", ((x0 + x1) / 2.0, (y0 + y1) / 2.0, h), -89.9, yaw, 40.0, True))
    # an office facade when an office is dressed
    off = next((x for x in layout["buildings"] if x.get("archetype") == "office" and dressable(x, table)[1] is not None), None)
    if off is not None:
        oe = K.edges_of(off)
        e = max(oe, key=lambda e: sum(1 for o in off.get("openings", []) or [] if K.edge_of_point(oe, o["pos"]) is e))
        c = e.at(e.L / 2.0)
        loc = vadd(c, vmul(e.n, 1.1 * e.L), vmul(e.d, -0.6 * e.L)) + (gz + 700.0,)
        pitch, yaw = look(loc, c + (gz + 300.0,))
        cams.append(("office_facade_34", loc, pitch, yaw, 50.0, False))
        CAM_NORMALS["office_facade_34"] = e.n
    return cams


def _opening_view(name, b, p, edges, cams, gz, z_at=160.0, dist=650.0, side=-260.0, height=230.0, nearest=False):
    """A close-up of an exterior opening at p from outside its facade (3/4, eye height); nothing when p is on no footprint side (nearest:
    the nearest side, for things standing off the wall - a fire escape's balconies)."""
    e = min(edges, key=lambda x: x.perp(p)) if nearest else K.edge_of_point(edges, p)
    if e is None:
        return
    loc = vadd(tuple(p), vmul(e.n, dist), vmul(e.d, side)) + (gz + height,)
    pitch, yaw = look(loc, tuple(p) + (gz + z_at,))
    cams.append((name, loc, pitch, yaw, 55.0, False))
    CAM_NORMALS[name] = e.n


def story_cameras(layout):
    """The story showcase views (docs/design/levelgen/STORY_SHOWCASE_2026-11-22.md): the whole map, every building's front 3/4 (the facade
    of its front door: the exterior ground door nearest the mandatory route), and the story elements of every building - a locked / sealed
    door, the rubble-blocked flight (roof off, from above) and the fire escape, the facade roof ladder, the broken back windows, the snow
    ramp. Same tuple as cameras(): (name, location, pitch, yaw, fov, hide roofs)."""
    gz = float(layout.get("ground_z", 0.0))
    (bx0, by0), (bx1, by1) = layout["bounds"]["min"], layout["bounds"]["max"]
    cx, cy = (bx0 + bx1) / 2.0, (by0 + by1) / 2.0
    span = max(bx1 - bx0, by1 - by0)
    cams = []
    loc = (bx0 - 0.35 * span, cy - 0.55 * span, gz + 0.55 * span)
    p, y = look(loc, (cx, cy, gz))
    cams.append(("overview_34", loc, p, y, 55.0, False))
    h = (by1 - by0 + 1500.0) / (2.0 * math.tan(math.radians(25.0))) + gz + 1000.0
    cams.append(("map_top_down", (cx, cy, h), -89.9, 0.0, 50.0, False))
    route = next((r["points"] for r in layout.get("routes", []) if r.get("mandatory")), [])

    def route_dist(q):
        best = float("inf")
        for a, c in zip(route, route[1:]):
            vx, vy = c[0] - a[0], c[1] - a[1]
            L2 = vx * vx + vy * vy or 1.0
            t = max(0.0, min(1.0, ((q[0] - a[0]) * vx + (q[1] - a[1]) * vy) / L2))
            best = min(best, math.hypot(q[0] - a[0] - t * vx, q[1] - a[1] - t * vy))
        return best

    for b in layout["buildings"]:
        if not b.get("enterable"):
            continue
        bid = b["id"]
        edges = K.edges_of(b)
        floors = int(b["floors"])
        H = 300.0 * floors
        ops = b.get("openings", []) or []
        doors = [o for o in ops if o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and K.edge_of_point(edges, o["pos"]) is not None]
        front = min(doors, key=lambda o: route_dist(o["pos"]), default=None)
        fe = K.edge_of_point(edges, front["pos"]) if front else max(edges, key=lambda e: e.L)
        c = fe.at(fe.L / 2.0)
        far = 0.75 * fe.L + 900.0
        loc = vadd(c, vmul(fe.n, far), vmul(fe.d, -0.45 * fe.L - 300.0)) + (gz + 700.0 + 100.0 * floors,)
        pitch, yaw = look(loc, c + (gz + H * 0.45,))
        cams.append(("%s_front_34" % bid, loc, pitch, yaw, 50.0, False))
        CAM_NORMALS["%s_front_34" % bid] = fe.n
        for o in doors:
            if o.get("state") in ("locked", "sealed", "barricaded"):
                _opening_view("%s_door_%s" % (bid, o["state"]), b, o["pos"], edges, cams, gz)
        broken = [o for o in ops if o.get("kind") == "window" and o.get("state") == "broken"]
        if broken:
            p0 = broken[len(broken) // 2]["pos"]
            _opening_view("%s_broken_windows" % bid, b, p0, edges, cams, gz, z_at=140.0, dist=900.0, side=-450.0, height=300.0)
        for st in b.get("stairs", []) or []:
            if not st.get("blocked"):
                continue
            far_pt = st.get("turn") or st["top"]
            v = K.unit((far_pt[0] - st["pos"][0], far_pt[1] - st["pos"][1]))
            z0 = gz + 300.0 * int(st["from"])
            back = (st["pos"][0] - v[0] * 160.0, st["pos"][1] - v[1] * 160.0)
            eye = back + (z0 + 175.0,)
            pitch, yaw = look(eye, (st["pos"][0] + v[0] * 150.0, st["pos"][1] + v[1] * 150.0, z0 + 60.0))
            cams.append(("%s_stair_%s_interior" % (bid, st["blocked"]), eye, pitch, yaw, 85.0, False))
            mid = ((st["pos"][0] + far_pt[0]) / 2.0, (st["pos"][1] + far_pt[1]) / 2.0)
            loc = (mid[0] + 450.0, mid[1] - 550.0, gz + 900.0)
            pitch, yaw = look(loc, mid + (z0 + 50.0,))
            # the flight sits under the upper slabs: a cutaway (roofs, floor slabs and the kit dressing hidden) shows the rubble boxes
            cams.append(("%s_stair_%s_cutaway" % (bid, st["blocked"]), loc, pitch, yaw, 55.0, "cutaway"))
            break
        for fx in b.get("fire_escapes", []) or []:
            at = fx.get("at") or fx.get("ground")
            _opening_view("%s_fire_escape" % bid, b, at, edges, cams, gz, z_at=300.0, dist=1100.0, side=-500.0, height=450.0, nearest=True)
            break
        for lad in layout.get("ladders", []) or []:
            if lad.get("id") == "LAD_%s_roof" % bid:
                _opening_view("%s_roof_ladder" % bid, b, lad["pos"], edges, cams, gz, z_at=H * 0.55, dist=1000.0, side=-450.0, height=350.0, nearest=True)
        for ramp in b.get("snow_ramps", []) or []:
            target = next((o for o in ops if o.get("id") == ramp.get("to_opening")), None)
            if target is None:
                continue
            fr = ramp["from"]
            v = K.unit((target["pos"][0] - fr[0], target["pos"][1] - fr[1]))
            side = (-v[1], v[0])
            loc = (fr[0] - v[0] * 500.0 + side[0] * 450.0, fr[1] - v[1] * 500.0 + side[1] * 450.0, gz + 350.0)
            pitch, yaw = look(loc, ((fr[0] + target["pos"][0]) / 2.0, (fr[1] + target["pos"][1]) / 2.0, gz + 200.0))
            cams.append(("%s_snow_drift" % bid, loc, pitch, yaw, 55.0, False))
    return cams


def game_world():
    return unreal.find_object(None, "%s.%s" % (TARGET, TARGET.rsplit("/", 1)[-1]))


def proxy_checks(w, layout, b):
    """In game: the hidden proxies still block sight and the navmesh, the openings stay open (sight trace + nav raycast through the street
    wall's solid end, its first ground window and its first entrance)."""
    gz = float(layout.get("ground_z", 0.0))
    edges = K.edges_of(b)
    e = street_edge_of(layout, b, edges)
    ops = [o for o in b.get("openings", []) or [] if K.edge_of_point(edges, o["pos"]) is e and int(o.get("floor", 0)) == 0]
    win = min((o for o in ops if o.get("kind") == "window"), key=lambda o: e.s_of(o["pos"]), default=None)
    door = next((o for o in ops if o.get("kind") == "door"), None)
    probes = [("solid wall end", e.at(20.0), True, True)]
    if win:
        probes.append(("window %s" % win["id"], tuple(win["pos"]), False, None))
    if door:
        probes.append(("entrance %s" % door["id"], tuple(door["pos"]), True, False))   # a closed leaf blocks sight, not the pawn (ADR-0003)
    ok = True
    for name, p, want_block, want_nav in probes:
        a2, b2 = vadd(p, vmul(e.n, 150.0)), vadd(p, vmul(e.n, -150.0))
        z = gz + (150.0 if "window" in name else 120.0)
        hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(a2[0], a2[1], z), unreal.Vector(b2[0], b2[1], z), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                    False, [], unreal.DrawDebugTrace.NONE, True)
        sight_blocked = hit is not None and bool(hit.to_tuple()[0])
        nav_txt = "-"
        if want_nav is not None:
            try:
                res = unreal.NavigationSystemV1.navigation_raycast(w, unreal.Vector(a2[0], a2[1], gz + 5.0), unreal.Vector(b2[0], b2[1], gz + 5.0))
                nav_blocked = bool(res[0] if isinstance(res, tuple) else res)
                nav_txt = "blocked" if nav_blocked else "open"
                ok = ok and nav_blocked == want_nav
            except Exception as exc:
                nav_txt = "n/a (%r)" % exc
        good = sight_blocked == want_block
        ok = ok and good
        REPORT.append("proxy check %-34s sight %-7s nav %-7s expected sight %s nav %s -> %s" % (
            name, "blocked" if sight_blocked else "open", nav_txt, "blocked" if want_block else "open",
            "-" if want_nav is None else ("blocked" if want_nav else "open"), "ok" if good else "FAIL"))
    REPORT.append("proxy checks: %s" % ("PASS" if ok else "FAIL"))


def count_kit(w):
    acts = [a for a in unreal.GameplayStatics.get_all_actors_with_tag(w, KIT_TAG)]
    comps = inst = 0
    for a in acts:
        for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            comps += 1
            inst += c.get_instance_count()
    proxies = unreal.GameplayStatics.get_all_actors_with_tag(w, PROXY_TAG)
    total = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
    REPORT.append("in game: %d kit actors, %d ISM components, %d instances; %d hidden proxies; %d actors in the world" % (len(acts), comps, inst, len(proxies), len(total)))


def shots():
    lj_path = os.path.join(PROJECT_DIR, "Content", "Data", "LevelJson", TARGET.rsplit("/", 1)[-1] + ".json")
    with open(lj_path, encoding="utf-8") as f:
        layout_rel = json.load(f)["levelgen_layout"]
    with open(os.path.join(PROJECT_DIR, layout_rel), encoding="utf-8") as f:
        layout = json.load(f)
    table = load_table()
    block = showcase_block(layout, table)
    all_cams = story_cameras(layout) if SHOT_SET == "story" else cameras(layout, table)
    cams = [c for c in all_cams if not SHOT_ONLY or c[0] in SHOT_ONLY]
    state = {"t": 0.0, "stage": 0, "shot": 0, "wait": 0.0, "handle": None, "hidden": []}
    REPORT.append("shots %s %s (prefix %s, layout %s)" % (TARGET, datetime.datetime.now().isoformat(), SHOT_PREFIX, layout_rel))
    names = ["%s%s.png" % (SHOT_PREFIX, c[0]) for c in cams]

    def finish():
        unreal.unregister_slate_post_tick_callback(state["handle"])
        os.makedirs(SHOTS_DIR, exist_ok=True)
        found = {}
        for root, _, files in os.walk(os.path.join(PROJECT_DIR, "Saved", "Screenshots")):
            for fn in files:
                if fn in names:
                    full = os.path.join(root, fn)
                    if fn not in found or os.path.getmtime(full) > os.path.getmtime(found[fn]):
                        found[fn] = full
        for fn, full in sorted(found.items()):
            shutil.copy2(full, os.path.join(SHOTS_DIR, fn))
        REPORT.append("copied %d / %d pictures to %s" % (len(found), len(names), SHOTS_DIR))
        REPORT.append("RESULT: %s" % ("PASS" if len(found) == len(names) else "FAIL"))
        with open(os.path.join(OUT_DIR, "kit_dress_shots.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(REPORT) + "\n")
        unreal.SystemLibrary.execute_console_command(game_world(), "quit")

    def tick(dt):
        try:
            state["t"] += dt
            w = game_world()
            if w is None:
                if state["t"] > 90:
                    REPORT.append("world %s not found" % TARGET)
                    finish()
                return
            if state["stage"] == 0:
                if state["t"] < 20.0:
                    return
                for cmd in ("ShowHUD", "DisableAllScreenMessages", "r.Streaming.FullyLoadUsedTextures 1", "Codex.Pressure.Enabled 0", "Codex.Pmc.Encounters 0"):
                    unreal.SystemLibrary.execute_console_command(w, cmd)
                cam = next((a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor) if CAM_TAG in [str(t) for t in a.tags]), None)
                if cam is None:
                    REPORT.append("no CameraActor tagged %s in the map" % CAM_TAG)
                    finish()
                    return
                state["cam"] = cam
                sun = next(iter(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.DirectionalLight)), None)
                if sun is not None:
                    fv = sun.get_actor_forward_vector()
                    state["sun"] = (fv.x, fv.y)
                count_kit(w)
                if block is not None:
                    proxy_checks(w, layout, block)
                cam.get_editor_property("camera_component").set_editor_property("constrain_aspect_ratio", False)
                roof_actors, roof_comps, slab_actors, kit_comps = [], [], [], []
                for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.StaticMeshActor):
                    tags = [str(t) for t in a.tags]
                    if "roof_slab" in tags or "kit_roof" in tags:
                        roof_actors.append(a)
                    if "floor_slab" in tags:
                        slab_actors.append(a)
                    if KIT_TAG in tags:
                        for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                            kit_comps.append(c)
                            if "kit_roof" in [str(t) for t in c.component_tags]:
                                roof_comps.append(c)
                state["roof"] = (roof_actors, roof_comps)
                # "cutaway" (story shots of a ground-floor element): roofs, every floor slab and the whole kit dressing hidden
                state["cutaway"] = (roof_actors + slab_actors, kit_comps)
                state["stage"] = 1
                state["wait"] = state["t"] + 4.0
                return
            if state["stage"] == 1 and state["t"] >= state["wait"]:
                for a in state["hidden"]:
                    if isinstance(a, unreal.Actor):
                        a.set_actor_hidden_in_game(False)
                    else:
                        a.set_hidden_in_game(False)
                state["hidden"] = []
                if state["shot"] >= len(cams):
                    finish()
                    return
                name, loc, pitch, yaw, fov, hide_roof = cams[state["shot"]]
                if hide_roof:
                    ra, rc = state["cutaway"] if hide_roof == "cutaway" else state["roof"]
                    for a in ra:
                        a.set_actor_hidden_in_game(True)
                    for c in rc:
                        c.set_hidden_in_game(True)
                    state["hidden"] = list(ra) + list(rc)
                cam = state["cam"]
                comp = cam.get_editor_property("camera_component")
                comp.set_editor_property("field_of_view", fov)
                ev = 1.5 if "interior" in name else 0.0   # the interiors: no light inside but the windows
                n = CAM_NORMALS.get(name)
                if n is not None and state.get("sun") is not None and n[0] * state["sun"][0] + n[1] * state["sun"][1] > 0.0:
                    ev = 1.0                               # a facade in the sun's shadow (the generated maps' sun is fixed)
                try:
                    pps = comp.get_editor_property("post_process_settings")
                    pps.set_editor_property("override_auto_exposure_bias", True)
                    pps.set_editor_property("auto_exposure_bias", ev)
                    comp.set_editor_property("post_process_settings", pps)
                    comp.set_editor_property("post_process_blend_weight", 1.0)
                except Exception as exc:
                    REPORT.append("exposure bias not set: %r" % exc)
                cam.set_actor_location_and_rotation(unreal.Vector(*loc), unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.0), False, True)
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(cam, 0.0)
                state["stage"] = 2
                state["wait"] = state["t"] + 3.0
                return
            if state["stage"] == 2 and state["t"] >= state["wait"]:
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(state["cam"], 0.0)
                name = cams[state["shot"]][0]
                unreal.SystemLibrary.execute_console_command(w, "HighResShot 1920x1080 filename=%s%s.png" % (SHOT_PREFIX, name))
                REPORT.append("shot %s: loc %s pitch %.1f yaw %.1f fov %.0f" % (name, [round(v) for v in cams[state["shot"]][1]], cams[state["shot"]][2], cams[state["shot"]][3], cams[state["shot"]][4]))
                state["shot"] += 1
                state["stage"] = 1
                state["wait"] = state["t"] + 5.0
        except Exception as exc:
            import traceback
            REPORT.append("EXCEPTION: %r\n%s" % (exc, traceback.format_exc()))
            finish()

    state["handle"] = unreal.register_slate_post_tick_callback(tick)


def flush_log():
    saved = os.path.join(PROJECT_DIR, "Saved", "Logs")
    os.makedirs(saved, exist_ok=True)
    with open(os.path.join(saved, "KitDressLayout.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")
    with open(os.path.join(saved, "KitDressLayout_%s.txt" % TARGET.rsplit("/", 1)[-1]), "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def main():
    global LAYOUT_FILE, BUILDER_ROOT
    if unreal is None:
        args = sys.argv[1:]
        if args and args[0] == "plan":
            if len(args) > 1:
                LAYOUT_FILE = os.path.abspath(args[1])
            if len(args) > 2:
                BUILDER_ROOT = os.path.abspath(args[2])
            _, _, specs, dressed, _ = plan_all()
            data = write_report(summary(specs, dressed), "kit_dress_plan_report.json")
            with open(os.path.join(OUT_DIR, "kit_dress_plan.json"), "w", encoding="utf-8") as f:
                json.dump({bid: d["instances"] for bid, d in dressed.items()}, f)
            print(json.dumps({"totals": data["totals"], "hidden_proxies": data["hidden_proxies"], "builder_specs": data["builder_specs"],
                              "buildings": {k: {x: v[x] for x in ("series", "ism_components", "instances", "hidden_proxies", "visible_builder_pieces")} for k, v in data["buildings"].items()}}, indent=1))
            return
        if args and args[0] == "export" and len(args) >= 2:
            export(args[1], args[2] if len(args) > 2 else None)
            return
        print(__doc__)
        return
    if MODE == "shots":
        shots()
        return
    try:
        report("kit_dress.py build %s <- %s (builder %s, dress %s) %s" % (TARGET, LAYOUT_FILE, BUILDER_ROOT, DRESS, datetime.datetime.now().isoformat()))
        build()
    except Exception as exc:
        import traceback
        report("ERROR: %r\n%s" % (exc, traceback.format_exc()))
    flush_log()


if unreal is not None or __name__ == "__main__":
    main()
