"""Slice math of dump_kit_interiors.py and room fill of kit_plan_images.py on tiny plans.

No editor: the two pure functions of the editor script are compiled from its source with
ast, so `import unreal` never runs.
"""
import ast
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import kit_plan_images as plan  # noqa: E402

EDITOR_SCRIPT = TOOLS.parent / "Editor" / "dump_kit_interiors.py"


def editor_functions(*names):
    tree = ast.parse(EDITOR_SCRIPT.read_text(encoding="utf-8"))
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    module = ast.Module(body=nodes, type_ignores=[])
    namespace = {}
    exec(compile(module, str(EDITOR_SCRIPT), "exec"), namespace)
    return [namespace[name] for name in names]


slice_triangles, upward_triangles_xy = editor_functions("slice_triangles", "upward_triangles_xy")


def box_triangles(x0, y0, z0, x1, y1, z1):
    """12 outward-wound (counter-clockwise seen from outside, z up) triangles of a box."""
    p = {
        "a": (x0, y0, z0), "b": (x1, y0, z0), "c": (x1, y1, z0), "d": (x0, y1, z0),
        "e": (x0, y0, z1), "f": (x1, y0, z1), "g": (x1, y1, z1), "h": (x0, y1, z1),
    }
    quads = ["adcb", "efgh", "abfe", "bcgf", "cdhg", "daeh"]
    tris = []
    for quad in quads:
        q = [p[key] for key in quad]
        tris.append((q[0], q[1], q[2]))
        tris.append((q[0], q[2], q[3]))
    return tris


def rect_segments(x0, y0, x1, y1):
    """Closed counter-clockwise rectangle as four segments."""
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return [corners[i] + corners[(i + 1) % 4] for i in range(4)]


def wall(mesh, *rects):
    segments = []
    for rect in rects:
        segments.extend(list(seg) for seg in rect_segments(*rect))
    return {"mesh": mesh, "segments": segments}


def item(mesh, low, high):
    return {"mesh": mesh, "min": list(low), "max": list(high)}


def two_room_storey(with_door=True, with_entrance=True):
    """6 m x 4 m house, 20 cm walls, a partition at x = 3 m with a 90 cm doorway.

    The interior door leaf stands open at 90 degrees into the right room. The front wall
    has a 1 m street door. Each room has one window in the back wall.
    """
    walls = [
        wall("SM_SH_Wall_Panel_solid_28", (0, 0, 600, 20), (0, 0, 20, 400), (580, 0, 600, 400),
             (0, 380, 100, 400), (200, 380, 600, 400)),
        wall("SM_SH_B99_Interior_F0", (290, 20, 310, 150), (290, 240, 310, 380)),
    ]
    items = [
        item("SM_SH_Win_Wood_win_28", (100, 2, 80), (200, 18, 230)),
        item("SM_SH_Win_Wood_win_28", (400, 2, 80), (500, 18, 230)),
    ]
    if with_door:
        items.append(item("SM_SH_Door_Int", (310, 146, 0), (390, 154, 200)))
    if with_entrance:
        items.append(item("SM_SH_Ent_Door_Wood", (100, 384, 0), (200, 390, 210)))
    return {
        "kind": "ground",
        "floor_z": 0.0,
        "cut_z": 100.0,
        "footprint_triangles": [[[0, 0], [600, 0], [600, 400]], [[0, 0], [600, 400], [0, 400]]],
        "walls": walls,
        "items": items,
    }


class SliceTest(unittest.TestCase):
    def test_box_slice_is_one_closed_square(self):
        segments = slice_triangles(box_triangles(0, 0, 0, 100, 50, 30), 10.0)
        self.assertEqual(len(segments), 8)  # two triangles per side face
        loops, chains = plan.chain_loops(segments)
        self.assertEqual(chains, [])
        self.assertEqual(len(loops), 1)
        self.assertAlmostEqual(abs(plan.signed_area(loops[0])), 100.0 * 50.0, places=3)

    def test_slice_orientation_follows_outward_normals(self):
        loops, _chains = plan.chain_loops(slice_triangles(box_triangles(0, 0, 0, 10, 10, 10), 5.0))
        outer = plan.signed_area(loops[0])
        loops, _chains = plan.chain_loops(slice_triangles(box_triangles(2, 2, 0, 4, 4, 10), 5.0))
        self.assertGreater(outer * plan.signed_area(loops[0]), 0.0)

    def test_plane_outside_or_on_a_face_gives_nothing_extra(self):
        self.assertEqual(slice_triangles(box_triangles(0, 0, 0, 10, 10, 10), 20.0), [])
        on_top = slice_triangles(box_triangles(0, 0, 0, 10, 10, 10), 10.0)
        self.assertLessEqual(len(on_top), 8)

    def test_upward_triangles_are_top_and_bottom_faces(self):
        flat = upward_triangles_xy(box_triangles(0, 0, 0, 10, 10, 10))
        self.assertEqual(len(flat), 4)


class RasterTest(unittest.TestCase):
    def test_chain_uses_a_reversed_segment(self):
        segments = rect_segments(0, 0, 10, 10)
        x0, y0, x1, y1 = segments[2]
        segments[2] = (x1, y1, x0, y0)
        loops, chains = plan.chain_loops(segments)
        self.assertEqual((len(loops), len(chains)), (1, 0))

    def test_nested_loop_is_a_hole(self):
        grid = plan.Grid(-10, -10, 410, 410, cell=5.0)
        mask = grid.empty()
        outer = [(0, 0), (400, 0), (400, 400), (0, 400)]
        hole = [(20, 20), (20, 380), (380, 380), (380, 20)]
        plan.paint_loops(mask, grid, [outer, hole])
        col, row = grid.cell_of(200, 200)
        self.assertFalse(mask[row, col])
        col, row = grid.cell_of(10, 200)
        self.assertTrue(mask[row, col])

    def test_open_door_leaf_finds_the_doorway(self):
        storey = two_room_storey()
        result = plan.analyse_storey(storey)
        door = result["doors"]["door_int"][0]
        self.assertTrue(door["found"])
        low, high = door["probe"]
        self.assertLess(abs((low[0] + high[0]) * 0.5 - 300.0), 15.0)  # on the partition line


class RoomFillTest(unittest.TestCase):
    def test_two_rooms_split_by_the_door(self):
        result = plan.analyse_storey(two_room_storey())
        areas = sorted(info["area_m2"] for info in result["rooms"].values())
        self.assertEqual(len(areas), 2)
        for area in areas:
            self.assertAlmostEqual(area, 2.7 * 3.6, delta=0.35)

    def test_without_a_door_the_doorway_merges_rooms(self):
        result = plan.analyse_storey(two_room_storey(with_door=False))
        self.assertEqual(len(result["rooms"]), 1)
        self.assertAlmostEqual(list(result["rooms"].values())[0]["area_m2"], 2 * 2.7 * 3.6 + 0.18, delta=0.6)

    def test_street_door_makes_one_flat_of_two_rooms(self):
        result = plan.analyse_storey(two_room_storey())
        self.assertEqual(len(result["flats"]), 1)
        flat = result["flats"][0]
        self.assertEqual(len(flat["rooms"]), 2)
        # The windowed 9.7 m2 entry room is below the 12 m2 "living (entry)" bar.
        self.assertEqual(flat["living_rooms"], 1)
        kinds = sorted(result["rooms"][value]["kind"] for value in flat["rooms"])
        self.assertEqual(kinds, ["hall+kitchen (open)", "living"])
        self.assertTrue(all(result["rooms"][value]["windows"] for value in flat["rooms"]))

    def test_room_kinds_by_area_and_window(self):
        base = {"doors": 1, "windows": [], "stair": False, "entry": False}
        self.assertEqual(plan.classify_room(dict(base, area_m2=3.0)), "bath/WC")
        self.assertEqual(plan.classify_room(dict(base, area_m2=6.0, windows=["w"])), "kitchen")
        self.assertEqual(plan.classify_room(dict(base, area_m2=14.0, windows=["w"])), "living")
        self.assertEqual(plan.classify_room(dict(base, area_m2=4.0, entry=True)), "hall")
        self.assertEqual(plan.classify_room(dict(base, area_m2=9.0, doors=3)), "corridor")


if __name__ == "__main__":
    unittest.main()
