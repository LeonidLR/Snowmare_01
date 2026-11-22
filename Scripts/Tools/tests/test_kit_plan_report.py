"""Two fake kit buildings: storeys, entrances, flats per landing, grid misfits."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import kit_plan_report as report


def actor(mesh_name, bounds_min, bounds_max, folder):
    low = [float(value) for value in bounds_min]
    high = [float(value) for value in bounds_max]
    center = [(low[axis] + high[axis]) * 0.5 for axis in range(3)]
    return {
        "actor": mesh_name,
        "actor_class": "StaticMeshActor",
        "component_class": "StaticMeshComponent",
        "instance": None,
        "mesh": "/Game/SovietHouses/Meshes/%s/%s" % (folder, mesh_name),
        "mesh_name": mesh_name,
        "location": center,
        "rotation": [0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0],
        "bounds_min": low,
        "bounds_max": high,
    }


def fake_dump():
    """Building 01 is a 9 m x 6 m panel block, three storeys. Building 02 is a plank house."""
    building_01 = [
        actor("SM_SH_B01_Slab_F0", [0, 0, -10], [900, 600, 10], "Buildings"),
        actor("SM_SH_B01_Slab_F1", [0, 0, 290], [900, 600, 310], "Buildings"),
        actor("SM_SH_B01_Slab_F2", [0, 0, 590], [900, 600, 610], "Buildings"),
        actor("SM_SH_Ent_Door_Steel", [390, 0, 0], [510, 20, 230], "Openings"),
        actor("SM_SH_Stair_ground_28_6", [310, 40, 0], [590, 320, 300], "Interior"),
        actor("SM_SH_StairGlass_PVC", [320, 50, 300], [580, 300, 600], "Openings"),
        actor("SM_SH_IntWall_stair_26", [300, 40, 0], [320, 300, 280], "Interior"),
        actor("SM_SH_Door_Apt", [200, 180, 300], [290, 200, 510], "Openings"),
        actor("SM_SH_Door_Apt", [600, 180, 300], [690, 200, 510], "Openings"),
        actor("SM_SH_Door_Apt", [200, 180, 600], [290, 200, 810], "Openings"),
        actor("SM_SH_Door_Apt", [600, 180, 600], [690, 200, 810], "Openings"),
        actor("SM_SH_IntWall_part_30", [100, 250, 300], [400, 265, 580], "Interior"),
        actor("SM_SH_IntWall_part_30", [500, 250, 300], [800, 265, 580], "Interior"),
        actor("SM_SH_IntWall_part_30", [100, 250, 600], [400, 265, 880], "Interior"),
        actor("SM_SH_IntWall_part_30", [500, 250, 600], [800, 265, 880], "Interior"),
        actor("SM_SH_Wall_Panel_win_28", [0, 580, 300], [300, 600, 580], "Facade"),
        actor("SM_SH_Win_PVC_pair_28", [100, 585, 350], [380, 600, 510], "Openings"),
        actor("SM_SH_Balc_Panel", [300, 600, 300], [600, 720, 410], "Balconies"),
        actor("SM_SH_Win_PVC_shop_28", [0, 0, 0], [280, 20, 220], "Openings"),
        actor("SM_SH_B01_Roof_0", [0, 0, 880], [900, 600, 920], "Buildings"),
    ]
    building_02 = [
        actor("SM_SH_B02_Slab_F0", [0, 0, -10], [500, 400, 10], "Buildings"),
        actor("SM_SH_B02_Slab_F1", [0, 0, 270], [500, 400, 290], "Buildings"),
        actor("SM_SH_Ent_Door_Wood", [0, 150, 0], [20, 270, 230], "Openings"),
        actor("SM_SH_Stair_mid_28_9", [40, 100, 0], [200, 300, 280], "Interior"),
        actor("SM_SH_Door_Apt", [80, 140, 280], [170, 160, 490], "Openings"),
        actor("SM_SH_Wall_Planks_win_28", [0, 380, 0], [280, 400, 280], "Facade"),
        actor("SM_SH_Dormer_24", [100, 100, 500], [340, 250, 680], "Roof"),
    ]
    return {
        "schema": "codex.kit_buildings/1",
        "units": "cm",
        "buildings": [
            {"id": "01", "map": "/Game/SovietHouses/Maps/Buildings/L_SH_Building_01",
             "loaded": True, "actors": building_01},
            {"id": "2", "map": "/Game/SovietHouses/Maps/Buildings/L_SH_Building_02",
             "loaded": True, "actors": building_02},
        ],
        "meshes": [
            {"path": "/Game/SovietHouses/Meshes/Openings/SM_SH_Win_PVC_pair_28",
             "name": "SM_SH_Win_PVC_pair_28", "size": [280, 15, 160]},
            {"path": "/Game/SovietHouses/Meshes/Facade/SM_SH_Wall_Panel_win_28",
             "name": "SM_SH_Wall_Panel_win_28", "size": [300, 20, 280]},
            {"path": "/Game/SovietHouses/Meshes/Buildings/SM_SH_B02_Slab_F0",
             "name": "SM_SH_B02_Slab_F0", "size": [500, 400, 20]},
        ],
    }


class KitPlanReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dump = fake_dump()
        cls.plans = report.build_plans(cls.dump, source="fake")
        cls.by_id = {plan["id"]: plan for plan in cls.plans["buildings"]}

    def test_storeys_and_height(self):
        panel = self.by_id["01"]
        planks = self.by_id["02"]
        self.assertEqual(panel["storeys"], 3)
        self.assertEqual(panel["slab_floors"], [0, 1, 2])
        self.assertAlmostEqual(panel["storey_height_m"], 3.0, places=2)
        self.assertEqual([panel["footprint_m"]["width"], panel["footprint_m"]["depth"]], [9.0, 6.0])
        self.assertEqual(planks["storeys"], 2)
        self.assertAlmostEqual(planks["storey_height_m"], 2.8, places=2)

    def test_entrances_stairwells_and_flats_per_landing(self):
        panel = self.by_id["01"]
        planks = self.by_id["02"]
        self.assertEqual(len(panel["entrances"]), 1)
        self.assertEqual(panel["entrances"][0]["mesh"], "SM_SH_Ent_Door_Steel")
        self.assertEqual(panel["entrances"][0]["side"], "court")
        self.assertEqual(panel["sections"], 1)
        self.assertEqual(panel["stairwells"][0]["side"], "court")
        self.assertEqual(panel["flat_door_source"], "Door_Apt")
        self.assertEqual(panel["flats_per_landing"], 2)
        self.assertEqual(sorted(row["flat_doors"] for row in panel["landings"]), [2, 2])
        self.assertAlmostEqual(panel["rooms_per_flat"], 2.0, places=2)
        self.assertEqual(planks["entrances"][0]["side"], "end")
        self.assertEqual(planks["flats_per_landing"], 1)

    def test_facades_roof_and_series(self):
        panel = self.by_id["01"]
        planks = self.by_id["02"]
        self.assertEqual(panel["balconies_by_facade"]["street"], 1)
        self.assertEqual(panel["balconies_by_facade"]["court"], 0)
        self.assertGreaterEqual(panel["shop_windows"], 1)
        self.assertEqual(panel["shop_doors"], 0)
        self.assertIn("PVC_pair", panel["window_types_by_facade"]["street"])
        self.assertEqual(panel["facade_modules"], [
            {"side": "street", "kind": "win", "width_cm": 280, "count": 1},
        ])
        self.assertEqual(panel["roof_type"], "flat")
        self.assertIn("panel", panel["series_guess"])
        self.assertFalse(panel["l_shape"])
        self.assertEqual(planks["roof_type"], "pitched_dormer")
        self.assertIn("barracks", planks["series_guess"])

    def test_grid_misfits(self):
        panel = self.by_id["01"]
        planks = self.by_id["02"]
        self.assertIn("SM_SH_Win_PVC_pair_28", panel["grid"]["300"]["misfit_names"])
        self.assertIn("SM_SH_Win_PVC_pair_28", panel["grid"]["50"]["misfit_names"])
        self.assertNotIn("SM_SH_Wall_Panel_win_28", panel["grid"]["300"]["misfit_names"])
        self.assertNotIn("SM_SH_Wall_Panel_win_28", panel["grid"]["50"]["misfit_names"])
        self.assertGreater(panel["grid"]["300"]["fit"], 0)
        self.assertIn("SM_SH_B02_Slab_F0", planks["grid"]["300"]["misfit_names"])
        self.assertNotIn("SM_SH_B02_Slab_F0", planks["grid"]["50"]["misfit_names"])
        catalog = {row["name"]: row for row in self.plans["mesh_modules"]}
        self.assertFalse(catalog["SM_SH_Win_PVC_pair_28"]["fit_300"])
        self.assertFalse(catalog["SM_SH_Win_PVC_pair_28"]["fit_50"])
        self.assertEqual(catalog["SM_SH_Win_PVC_pair_28"]["nominal_cm"], 280)
        self.assertFalse(catalog["SM_SH_Win_PVC_pair_28"]["module_fit_300"])
        self.assertFalse(catalog["SM_SH_Win_PVC_pair_28"]["module_fit_50"])
        # The panel name says 280 cm. Its box also has a 300 cm side and a 280 cm side,
        # so the whole box misses 300 cm even when a placed 300 cm edge sits on the grid.
        self.assertEqual(catalog["SM_SH_Wall_Panel_win_28"]["nominal_cm"], 280)
        self.assertFalse(catalog["SM_SH_Wall_Panel_win_28"]["fit_300"])
        self.assertIn(300.0, catalog["SM_SH_Wall_Panel_win_28"]["widths_cm"])
        self.assertFalse(catalog["SM_SH_B02_Slab_F0"]["fit_300"])
        self.assertTrue(catalog["SM_SH_B02_Slab_F0"]["fit_50"])
        self.assertEqual(catalog["SM_SH_B02_Slab_F0"]["module_cm"], 500.0)
        self.assertFalse(catalog["SM_SH_B02_Slab_F0"]["module_fit_300"])
        self.assertTrue(catalog["SM_SH_B02_Slab_F0"]["module_fit_50"])

    def test_summary_and_files(self):
        self.assertEqual([row["id"] for row in self.plans["summary"]], ["01", "02"])
        text = report.render_markdown(self.plans)
        self.assertIn("panel", text)
        self.assertIn("barracks", text)
        with tempfile.TemporaryDirectory() as tmp:
            source = os.path.join(tmp, "kit_buildings.json")
            with open(source, "w", encoding="utf-8") as handle:
                json.dump(self.dump, handle)
            out_dir = os.path.join(tmp, "out")
            code = report.main(["--input", source, "--out-dir", out_dir])
            self.assertEqual(code, 0)
            with open(os.path.join(out_dir, "kit_plans.json"), encoding="utf-8") as handle:
                written = json.load(handle)
            self.assertEqual(written["buildings"][0]["flats_per_landing"], 2)
            with open(os.path.join(out_dir, "kit_plans.md"), encoding="utf-8") as handle:
                markdown = handle.read()
            self.assertIn("Building 01", markdown)
            self.assertIn("SM_SH_Win_PVC_pair_28", markdown)


if __name__ == "__main__":
    unittest.main()
