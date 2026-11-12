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
        self.assertEqual(sha, "9a37162a0b7bc284")

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
        # Window sills: height 90 cm (<= 105 VaultRules::MaxHeight) + CodexMantleEdge tag
        for s in window_sills:
            self.assertAlmostEqual(s["scale"][2] * 100.0, 90.0, places=1)
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
        # Loophole lintels start at >= 175 cm (so Standing EyeHeight 160 passes unobstructed)
        bunker_lintels = [
            s for s in specs
            if "opening_lintel" in s.get("tags", [])
            and "building=B_bunker" in s.get("tags", [])
            and "door_lintel" not in s.get("tags", [])
        ]
        self.assertEqual(len(bunker_lintels), 7)  # B_bunker has 7 loopholes + 1 door
        for s in bunker_lintels:
            bottom_z = s["location"][2] - s["scale"][2] * 50.0
            self.assertGreaterEqual(bottom_z, 175.0)

        # Free-standing tower hatch ladder: wall_bracket_count=-1, top_exit_standoff=110, 3-sided hatch rim
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertEqual(ladders["LAD_B_tower_hatch"].get("wall_bracket_count"), -1)
        self.assertAlmostEqual(ladders["LAD_B_tower_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
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

        # B_catwalk roof parapet S ([550, 1150]..[550, 2750]) must be cut around STAIR_catwalk_ext at y=2600
        cat_s_parapets = [s for s in specs if s["label"].startswith("B_catwalk_parapet_S")]
        self.assertGreaterEqual(len(cat_s_parapets), 2)
        for p in cat_s_parapets:
            ly = p["location"][1]
            sy = p["scale"][1] * 100.0
            self.assertFalse(ly - sy * 0.5 < 2600.0 < ly + sy * 0.5, "External stair arrival on B_catwalk_parapet_S is blocked!")

        # B_warehouse hatch [-...] -> 3-sided rim, 110 standoff, roof cutout, floor 1 cutout? (hatch floor=1 -> only roof floor=2 cut)
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertAlmostEqual(ladders["LAD_B_warehouse_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
        self.assertEqual(ladders["LAD_B_warehouse_hatch"].get("wall_bracket_count"), -1)

        # Covers with mantle_edges get CodexMantleEdge
        mantle_covers = [s for s in specs if s["folder"] == "Covers" and "CodexMantleEdge" in s.get("tags", [])]
        self.assertGreater(len(mantle_covers), 0)


if __name__ == "__main__":
    unittest.main()
