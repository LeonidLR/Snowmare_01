"""Unit tests for build_levelgen_map.py combat-building extensions (028) and legacy Depot parity."""

import hashlib
import json
import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402
import levelgen_bot_route  # noqa: E402


class BuildLevelGenBuildingsTest(unittest.TestCase):
    def _load(self, name):
        return builder.load_layout(os.path.join(ACCEPTED_DIR, f"{name}.json"))

    def test_legacy_depot_bit_identical(self):
        depot = self._load("gen_ref_01_02_depot_1")
        specs = builder.plan(depot)
        self.assertEqual(builder.map_name_for(depot), "/Game/Maps/Generated/L_Gen_Depot_1")
        self.assertEqual(len(specs), 545)
        sha = hashlib.sha256(json.dumps(specs, sort_keys=True).encode()).hexdigest()[:16]
        # 2026-11-15 mantling slice 1: the designer's obstacle height rework (Scripts/Tools/apply_depot_height_classes.py) - was 9a37162a0b7bc284
        self.assertEqual(sha, "a93f43a8e33e0250")

    def test_train_1_openings_partitions_no_doubles_and_parapets(self):
        layout = self._load("L_Gen_Train_1")
        self.assertEqual(builder.map_name_for(layout), "/Game/Maps/Generated/L_Gen_Train_1")
        specs = builder.plan(layout)

        window_sills = [s for s in specs if "window_sill" in s.get("tags", [])]
        mantle_sills = [s for s in specs if "mantle_sill" in s.get("tags", [])]
        lintels = [s for s in specs if "opening_lintel" in s.get("tags", [])]
        parapets = [s for s in specs if "parapet" in s.get("tags", [])]
        interiors = [s for s in specs if "interior" in s.get("tags", []) and "opening_lintel" not in s.get("tags", [])]

        # B_house + B_garage: ground windows get vaultable window_sill, floor 1 windows get mantle_sill
        self.assertGreater(len(window_sills), 0)
        self.assertGreater(len(mantle_sills), 0)
        self.assertEqual(len(lintels), 25 + 4 + 8)  # 25 house + 4 garage + 8 partition doors
        # Window sills: measured low_vault target 95 cm (MANTLING.md 11, 58a7e34e) + CodexMantleEdge tag
        for s in window_sills:
            self.assertAlmostEqual(s["scale"][2] * 100.0, 95.0, places=1)
            self.assertIn("CodexMantleEdge", s["tags"])
        # No parapet may carry "Vault" tag
        for p in parapets:
            self.assertNotIn("Vault", p["tags"])
            self.assertIn("CodexMantleEdge", p["tags"])
        # Interior walls count matches layout interior walls (no double-spawning from b["partitions"])
        layout_interior_count = sum(1 for w in layout["walls"] if w.get("kind") == "interior")
        self.assertEqual(len(interiors), layout_interior_count)
        # Parapets cut around LAD_B_house exit on East edge -> >= 5 parapet pieces instead of 4 uncut sides
        self.assertGreaterEqual(len(parapets), 5)

    def test_train_2_hatches_loopholes_and_ladder_standoff(self):
        layout = self._load("L_Gen_Train_2")
        specs = builder.plan(layout)

        loophole_sills = [s for s in specs if "loophole_sill" in s.get("tags", [])]
        self.assertEqual(len(loophole_sills), 31)  # 22 checkpoint + 7 bunker + 2 tower
        for s in loophole_sills:
            self.assertAlmostEqual(s["scale"][2] * 100.0, 120.0, places=1)
        # Loophole lintels preserve exact JSON top_height (150 on B_bunker f=0 -> bottom Z 150.0, no hidden clamp)
        bunker_lintels = [
            s for s in specs
            if "opening_lintel" in s.get("tags", [])
            and "building=B_bunker" in s.get("tags", [])
            and "door_lintel" not in s.get("tags", [])
        ]
        self.assertEqual(len(bunker_lintels), 7)  # B_bunker has 7 loopholes + 1 door
        for s in bunker_lintels:
            bottom_z = s["location"][2] - s["scale"][2] * 50.0
            self.assertAlmostEqual(bottom_z, 150.0, places=1)

        # Free-standing tower hatch ladder: wall_bracket_count=-1, top_standoff=110, top_exit_standoff=110, 3-sided hatch rim
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertEqual(ladders["LAD_B_tower_hatch"].get("wall_bracket_count"), -1)
        self.assertAlmostEqual(ladders["LAD_B_tower_hatch"].get("top_standoff", 0.0), 110.0, places=1)
        self.assertAlmostEqual(ladders["LAD_B_tower_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
        self.assertNotIn("top_standoff", ladders["LAD_B_checkpoint"])
        self.assertNotIn("top_exit_standoff", ladders["LAD_B_checkpoint"])

        # Hatches NEVER produce DROP NavLinkProxy actors
        drops = levelgen_bot_route.drop_links(layout, 0.0)
        self.assertEqual(len(drops), 0)
        rims = [s for s in specs if "hatch_rim" in s.get("tags", [])]
        self.assertEqual(len(rims), 3)  # 3 non-exit sides

        # B_tower roof slab at X[-1400..-600], Y[-2250..-1450] must NOT cover hatch [-1270..-1170]x[-1680..-1580]
        tower_roof = [s for s in specs if "roof_slab" in s.get("tags", []) and "building=B_tower" in s.get("tags", [])]
        self.assertGreater(len(tower_roof), 1)
        for slab in tower_roof:
            sx, sy = slab["scale"][0] * 100.0, slab["scale"][1] * 100.0
            lx, ly = slab["location"][0], slab["location"][1]
            x0, x1 = lx - sx * 0.5, lx + sx * 0.5
            y0, y1 = ly - sy * 0.5, ly + sy * 0.5
            overlap_x = min(x1, -1170.0) - max(x0, -1270.0)
            overlap_y = min(y1, -1580.0) - max(y0, -1680.0)
            self.assertFalse(overlap_x > 1.0 and overlap_y > 1.0, f"Slab {slab['label']} covers B_tower hatch!")

        # Hatch rims sit strictly outside the hatch hole and skip the East exit side (x = -1170)
        for r in rims:
            self.assertFalse(r["label"].endswith("_rim_E"), f"Exit side rim must be omitted: {r['label']}")

    def test_train_3_catwalk_gaps_stair_parapet_cut_and_hatch(self):
        layout = self._load("L_Gen_Train_3")
        specs = builder.plan(layout)

        # B_catwalk roof parapet S ([550, 1150]..[550, 2750]) must leave the entire STAIR_catwalk_ext landing [2595..2750] clear
        cat_s_parapets = [s for s in specs if s["label"].startswith("B_catwalk_parapet_S")]
        self.assertGreaterEqual(len(cat_s_parapets), 1)
        for p in cat_s_parapets:
            ly = p["location"][1]
            sy = p["scale"][1] * 100.0
            y0, y1 = ly - sy * 0.5, ly + sy * 0.5
            self.assertFalse(y0 < 2750.0 and y1 > 2595.0, f"Parapet {p['label']} [{y0}..{y1}] overlaps STAIR_catwalk_ext landing [2595..2750]!")

        # B_warehouse hatch [-...] -> 3-sided rim, 110 standoff, roof cutout, floor 1 cutout? (hatch floor=1 -> only roof floor=2 cut)
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertAlmostEqual(ladders["LAD_B_warehouse_hatch"].get("top_standoff", 0.0), 110.0, places=1)
        self.assertAlmostEqual(ladders["LAD_B_warehouse_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
        self.assertEqual(ladders["LAD_B_warehouse_hatch"].get("wall_bracket_count"), -1)

        # Covers with mantle_edges get CodexMantleEdge
        mantle_covers = [s for s in specs if s["folder"] == "Covers" and "CodexMantleEdge" in s.get("tags", [])]
        self.assertGreater(len(mantle_covers), 0)

    def test_player_start_follows_objectives_start(self):
        """Designer bug 2026-11-16: the start dragged in Training Ground (objectives.start, source manual) drives the PlayerStart;
        a layout without objectives keeps the old spawn. In-memory copies only: no accepted file is written."""
        depot = self._load("gen_ref_01_02_depot_1")
        self.assertNotIn("objectives", depot)
        gz = float(depot.get("ground_z", 0.0))

        def player_start(layout):
            starts = [s for s in builder.plan(layout) if s["kind"] == "player_start"]
            self.assertEqual(len(starts), 1)
            return starts[0]

        old = player_start(depot)
        self.assertEqual(old["location"][:2], depot["spawn"]["pos"])
        self.assertEqual(old["yaw"], float(depot["spawn"]["yaw"]))
        self.assertAlmostEqual(old["location"][2], gz + builder.START_Z)
        moved = json.loads(json.dumps(depot))
        moved["objectives"] = {"version": 1, "seed": 0, "order": "free", "goals": [],
                               "start": {"pos": [2477, -6184], "yaw": 180, "layer": "ground", "source": "manual"}}
        ps = player_start(moved)
        self.assertEqual(ps["location"][:2], [2477, -6184])
        self.assertEqual(ps["yaw"], 180.0)
        self.assertAlmostEqual(ps["location"][2], gz + builder.START_Z)
        self.assertEqual(builder.start_of(moved), (2477, -6184, 180.0, 0))
        # a start without a yaw keeps the spawn's facing; an empty objectives block falls back to the spawn
        moved["objectives"]["start"].pop("yaw")
        self.assertEqual(builder.start_of(moved)[2], float(depot["spawn"]["yaw"]))
        moved["objectives"] = None
        self.assertEqual(player_start(moved)["location"], old["location"])

    # ---- rotated buildings (designer 2026-11-16 item 4) -------------------------------------------------------------------------

    @staticmethod
    def _rotate_building(layout, bid, deg):
        """The layout with building `bid` turned by deg about its footprint centre the way Training Ground stores it (rotation.ts):
        "rotation" set, footprint / room bounds unchanged, every other point of it, its walls and its ladders turned."""
        out = json.loads(json.dumps(layout))
        b = next(x for x in out["buildings"] if x["id"] == bid)
        (x0, y0), (x1, y1) = b["footprint"]
        c = ((x0 + x1) / 2.0, (y0 + y1) / 2.0)
        P = lambda q: builder._rot_xy(q, c, deg)  # noqa: E731
        b["rotation"] = deg
        for d in b.get("doors", []):
            d["pos"] = P(d["pos"])
        for st in b.get("stairs", []):
            for k in ("pos", "mid", "top"):
                if st.get(k):
                    st[k] = P(st[k])
        b["roof_holes"] = [P(h) for h in b.get("roof_holes", [])]
        for h in b.get("floor_holes", []):
            h["pos"] = P(h["pos"])
        for op in b.get("openings", []) + [o for pt in b.get("partitions", []) for o in pt.get("openings", [])]:
            op["pos"] = P(op["pos"])
            if isinstance(op.get("yaw"), (int, float)):
                op["yaw"] = op["yaw"] + deg
        for pt in b.get("partitions", []):
            pt["start"], pt["end"] = P(pt["start"]), P(pt["end"])
        for pp in b.get("parapets", []):
            pp["start"], pp["end"] = P(pp["start"]), P(pp["end"])
        for h in b.get("hatches", []):
            h["pos"] = P(h["pos"])
        for fd in b.get("floors_detail", []):
            for g in fd.get("gaps", []):
                g["poly"] = [P(q) for q in g["poly"]]
        for w in out["walls"]:
            if w.get("building") == bid:
                w["a"], w["b"] = P(w["a"]), P(w["b"])
        for ld in out["ladders"]:
            if bid in ld["from"] + ld["to"]:
                ld["pos"], ld["foot"], ld["top"], ld["yaw"] = P(ld["pos"]), P(ld["foot"]), P(ld["top"]), ld["yaw"] + deg
        return out, c

    def _assert_rotated_plan(self, layout, bid, deg):
        flat = builder.plan(layout)
        turned, c = self._rotate_building(layout, bid, deg)
        rot = builder.plan(turned)
        wall_ids = tuple(w["id"] for w in layout["walls"] if w.get("building") == bid)  # legacy: its walls go to Walls/L<n>
        mine = lambda s: s.get("folder", "") == "Buildings/%s" % bid or s.get("folder", "").startswith("Buildings/%s/" % bid) or "building=%s" % bid in s.get("tags", []) or s["label"].startswith(wall_ids)  # noqa: E731
        flat_b = {s["label"]: s for s in flat if mine(s)}
        rot_b = {s["label"]: s for s in rot if mine(s)}
        self.assertGreaterEqual(len(flat_b), 1)
        self.assertEqual(sorted(flat_b), sorted(rot_b), "the same actors, only turned")
        for label, s in flat_b.items():
            r = rot_b[label]
            want = builder._rot_xy(s["location"], c, deg)
            for k in range(3):
                self.assertAlmostEqual(r["location"][k], want[k], places=3, msg="%s location" % label)
            dyaw = (r["yaw"] - s["yaw"] - deg + 180.0) % 360.0 - 180.0
            self.assertAlmostEqual(dyaw, 0.0, places=3, msg="%s: actor yaw = layout rotation" % label)
            self.assertEqual(r["scale"] if "scale" in r else None, s["scale"] if "scale" in s else None, label)
        # everything else stays where it was (its ladders and the drop links through its holes move with it: world points)
        other = lambda s: not mine(s) and s["kind"] not in ("ladder", "nav_link") and not s["label"].endswith("_plinth")  # noqa: E731
        others_flat = sorted(json.dumps(s, sort_keys=True) for s in flat if other(s))
        others_rot = sorted(json.dumps(s, sort_keys=True) for s in rot if other(s))
        self.assertEqual(others_flat, others_rot)
        return rot

    def test_rotated_building_legacy_actors_turned_with_it(self):
        depot = self._load("gen_ref_01_02_depot_1")
        self.assertTrue(all(builder.building_rotation(b) == 0.0 for b in depot["buildings"]), "old files load as 0")
        for bid in ("B_warehouse", "B_ruin", "B_chimney_0"):
            self._assert_rotated_plan(depot, bid, 30.0)
        self._assert_rotated_plan(depot, "B_hall_n", -45.0)

    def test_rotated_building_extended_actors_turned_with_it(self):
        train = self._load("L_Gen_Train_1")
        rot = self._assert_rotated_plan(train, "B_house", 30.0)
        # its ladder (placed from the world frame) turns with the house: yaw = the unrotated yaw + 30
        flat_ld = next(s for s in builder.plan(train) if s["kind"] == "ladder" and s["label"] == "LAD_B_house")
        rot_ld = next(s for s in rot if s["kind"] == "ladder" and s["label"] == "LAD_B_house")
        self.assertAlmostEqual((rot_ld["yaw"] - flat_ld["yaw"] - 30.0 + 180.0) % 360.0 - 180.0, 0.0, places=2)


if __name__ == "__main__":
    unittest.main()
