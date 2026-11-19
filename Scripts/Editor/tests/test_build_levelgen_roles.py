"""Role defaults of the /2 opening-state table in build_levelgen_map.py (S2_ROOMS_PLAN.md 4.1; mirrors openingStates.ts): a 'portal' door
(the pad opening of a stair core, planned buildings) is 'open' wherever it sits - on a partition, on a facade, in a ruin - and an explicit
state still wins. Pure: no Unreal (python -m unittest discover -s Scripts/Editor/tests -p "test_build_levelgen_*.py")."""

import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402

V2 = builder.SCHEMA_V2

PORTAL = {"id": "B_f0_d3", "kind": "door", "floor": 0, "pos": [0, 0], "width": 200, "role": "portal", "module": "portal_200"}
ROOM_DOOR = {"id": "B_f0_d4", "kind": "door", "floor": 0, "pos": [10, 0], "width": 150, "role": "room", "module": "wall_door_200"}
ENTRY = {"id": "B_f0_d0", "kind": "door", "floor": 0, "pos": [0, 500], "wall": "B_S", "width": 150, "role": "entry", "module": "wall_door_300"}


def _building(set_piece="facade"):
    return {"id": "B", "set_piece": set_piece, "openings": [ENTRY],
            "partitions": [{"id": "B_part_f0_0", "floor": 0, "openings": [PORTAL, ROOM_DOOR]}]}


class PortalDefaultTest(unittest.TestCase):
    def setUp(self):
        self.layout = {"schema": V2, "walls": []}

    def state(self, op, building=None, layout=None):
        return builder.effective_opening_state(layout or self.layout, building or _building(), op)

    def test_portal_on_a_partition_is_open(self):
        self.assertEqual(self.state(PORTAL), "open")

    def test_portal_that_classifies_as_exterior_is_still_open(self):
        # the same door left out of every partition = a facade opening: a plain door would be 'closed', the role makes it 'open'
        building = {"id": "B", "set_piece": "facade", "openings": [PORTAL], "partitions": []}
        self.assertTrue(builder.classify_opening(self.layout, building, PORTAL)[0])
        self.assertEqual(self.state(PORTAL, building), "open")
        self.assertEqual(self.state(dict(PORTAL, role="room"), building), "closed")

    def test_other_roles_keep_the_table(self):
        self.assertEqual(self.state(ENTRY), "closed")       # exterior door
        self.assertEqual(self.state(ROOM_DOOR), "open")     # interior door

    def test_portal_in_a_ruin_is_open_and_an_explicit_state_wins(self):
        self.assertEqual(self.state(PORTAL, _building("ruined_two_level")), "open")
        self.assertEqual(self.state(dict(PORTAL, state="sealed")), "sealed")

    def test_v1_ignores_roles(self):
        v1 = {"schema": builder.SCHEMA, "walls": []}
        self.assertEqual(self.state(PORTAL, layout=v1), "open")
        self.assertEqual(self.state(ENTRY, layout=v1), "open")


if __name__ == "__main__":
    unittest.main()
