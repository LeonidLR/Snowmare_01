# Codex 104-tg-stale-bgen-x6 (after 103)

Designer 2026-11-24: the Training Ground says "the map has no level JSON (Content/Data/LevelJson/L_Gen_Bgen_Test_X6.json):
weather cannot be saved; it is created when the map is built in Unreal" while he works on X6.
The real X6 is L_Gen_Depot_1_X6 (map + LevelJson exist). Stale leftovers of the old bgen-test X6 exist:
Content/Data/LevelGen/props/L_Gen_Bgen_Test_X6.props.json and Content/Data/LevelGen/tactical/L_Gen_Bgen_Test_X6.tactical.json.
Find why the TG offers / opens L_Gen_Bgen_Test_X6 (map list from props / tactical files?), make it list only the 5 kept maps
(memory four-maps-only: StoryShowcase_Kit, Depot_1_X6, Demo01_A_FrozenLake, VehicleTest, MainMenu) and check whether the
Bgen_Test_X6 props / tactical data must move to L_Gen_Depot_1_X6 (compare with the existing Depot_1_X6 props file first; do not
delete - move stale files to the archive per the 061 map archive convention). Branch codex/104-tg-stale-x6, TG tests.
