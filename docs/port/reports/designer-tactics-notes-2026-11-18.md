# Designer's tactics notes from the trainer clips, 2026-11-18

These notes are collected first and then handed to the lead as one task (PMC commander AI). Worker-ops does not change the AI.

## 1. Roof defence: the defenders bunch up in the far corner

**What the clip shows.** Probably a pmc-v5 roofs-scenario round on Depot_1. All three defenders stand together in the far corner of the roof.

**What the designer expects:**
- **Sectors.** Two fighters each cover the approaches from one side. They watch the sides the enemy can come from, starting with the enemy's general direction, which they know.
- **Hold the ground below.** They keep the enemy away from the building for as long as possible: fire down from the roof and change positions along the roof edge. The goal is to stop the enemy from ever reaching the roof.
- **Guard the roof entrance.** The third fighter covers the roof access (ladder or stairs) with his weapon aimed straight at it. If he can, he mines it or sets a tripwire there.

**Likely cause** (to be checked by the lead):
- The objective order pulls every defender into the capture circle. Objective is 17-23 % of all orders.
- There is no role split: no sector assignment and no "guard the access point" order.
- Defensive placement (mines, tripwire) is not in the order catalog.

### 1b. The same problem with an outside ladder (blizzard clip, the building with two roof hatches)

**What the clip shows.** Again the defenders bunch together in one corner of the roof, and the approaches to the building are not covered at all. Here the ladder runs up the outside wall, at the side of the building. In the first clip the access was from inside the building.

**What the designer expects:**
- The fighter guarding the access stands where he covers both the ladder climb and the street next to it.
- The other two fighters cover the approaches from their sides, as in note 1.

**Rule for the lead.** The guard's position depends on the access type:
- **Inside access** (hatch or stairwell): aim at the hatch, mine it or put a tripwire on it.
- **Outside ladder:** stand at the roof edge above the ladder, so that both the ladder and the adjoining street are in the field of fire.

## 2. Holding a point on the ground: mine the approaches, one fighter per piece of cover, tripwire on the flank

**Situation.** The defenders hold a point on the ground and cannot get onto a roof. The clip shows the green attackers running straight in and into a building.

**What the designer expects:**
- **Mines on the approaches** (yellow on the sketch). Lay them on the direct line between the attackers and the point, so the enemy cannot run straight in and has to take a detour.
- **Mine the doorway** the attackers use, so they cannot enter the building unpunished.
- **Spread over separate cover** (red on the sketch). Each of the three defenders hides behind his own piece of cover, never all three behind one.
- **Tripwire on the approach to the defenders' own position** (blue on the sketch). The enemy cannot rush them without paying for it.

**Clarification from the designer.** The mines do not forbid a straight run. The enemy may still run straight, but with a high chance of being blown up. The point is that the attackers take losses before they even reach the defenders.
- **How he plays it himself:** when he has to hold a point, the first thing he does is mine the approaches.
- **The attackers' answer:** an attacker who spots a mine can defuse it. That is fine and part of the game. So the attackers need two behaviours:
  - spot a mine, then defuse it or go around;
  - weigh a straight run against the risk.
- **Reference data:** the planned "shadow" recording of the designer's own play (his actions over time) will show this order directly: mine first, then take cover. Use it as reference data for the defender behaviour.

**What is missing now** (for the lead):
- There is no order "lay mines / a tripwire on an approach before contact". Before the first contact, the defender side has time for this; that is the setup phase.
- The approach lines and the doorways the enemy takes come from the tactical map: routes, chokepoints and doors (TACTICAL_MAP).
- Assigning cover per fighter: one fighter per cover object, with a minimum spacing between fighters.

## 3. The fourth clip cannot be read

This is probably the QD clip on L_Gen_Depot_X4, `qd-x4-g0003_c02-flank-ambush-168s`. The designer could not tell what happens in it: the fighters are too far apart and the action is scattered.

This is the X4 clip camera task (worker-ops / Claude). The camera has to frame the action, meaning the fighters who are in contact, and not the whole large map.

---

**Data to check (all notes):**
- In the roofs-scenario tracks of pmc-v5 / pmc-v6-qd, measure the spread of the defenders' positions (mean distance between them).
- Find how often a defender ends up within N m of the roof access.
