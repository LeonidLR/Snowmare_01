# Reference 01_02 "Depot" - the game designer's annotated Fallout Tactics map "Great Bend" (read 2026-11-01)

> Mission 2 "Depot" is the existing arctic level `L_Demo01_B_FrozenFactory`. The designer annotated another game's map
> (`Saved/MapRefs/Map_Ref_01_02_Depot.jpg`; the clean render `FOT_Map_Great_Bend.webp`, 3840 x 2160) as the example of SCALE
> and of the PLACEMENT of obstacles, ladders, barricades, enterable buildings and sightlines for our future mission-2 map.
> Its structure, proportions, densities and tactical patterns are the model - never a 1:1 copy of its layout or art.
> Machine-readable twin: `Content/Data/LevelGen/refs/ref_01_02_depot.json` (schema `codex.levelgen_ref/1`), the numbers the
> generator (`docs/design/levelgen/LEVEL_GENERATOR.md`) targets. Schematic for the designer:
> `Saved/Screenshots/LevelGen/ref_01_02_depot_schematic.png` (over the un-skewed map) and `..._plain.png`.

## 1. How it was read (so the reading can be checked)

- The annotations were pulled out by differencing the annotated JPEG against the clean render: 23 blue route segments,
  4 yellow ladder marks, 1 green start circle (`Scripts/Tools/levelgen_render.py unskew`, the scratch scripts of the session).
- The iso rhombus (its four corners at px 62/1141, 2271/40, 3788/872, 1530/2103) was un-skewed by a least-squares affine to a
  top-down frame: **x east** along the long edge (left corner -> top corner), **y south** along the short edge, origin = the
  west corner. All coordinates below are metres in that frame. The residual of the four-corner fit is 35 px (the map's vignette
  eats the corners), i.e. +-1.5 m on the far side.
- **Scale.** The source art is not to one scale: human sprites and oil drums give 20 px / m (the FOT hex = 1 m; "scale A"),
  vehicles, buildings and street widths give 12.5 px / m ("scale B": a sedan 4.5 m, the pump house 12 x 10 m, 2-lane streets
  7-9 m, chain-link panels 1.4 m). FOT draws its people oversized against its buildings. For a map our 1.8 m operatives walk in
  formation (2.6-2.8 m spacing, 50 m sight), the ARCHITECTURAL scale is the one to keep: **scale B, the map is ~177 x 124 m**
  (2.2 ha). Scale A would make it 110 x 77 m and every street 4-5 m wide (too tight for a diamond). The profile carries both;
  the generator's size parameter covers the range. The coordinator's "200 x 150 m or more" is scale B plus margin.
- **Our current ground plane.** `L_Demo01_B_FrozenFactory` is a copy of L_MovementTest: ~60 m wide (its side walls at X -3800 /
  2200) by ~160 m long with the lake compound (Floor2 to Y -7850, the compound floor to Y -12050). The reference needs about
  twice the area and three times the width: **a generated map gets its own ground plane, 200 x 150 m (layout + 10 m margin)**,
  never a resize of the Frozen Factory map (which is not touched).

## 1a. Height levels (game designer decision 2026-11-01, night)

Every map is split into discrete height levels 3 m apart, and the layout layers, the generator's grammar, the simulator's
per-level evaluation and the UE builder use them (`Tools/TrainingGround/src/levelgen/schema.ts` `LEVEL_HEIGHT`, the profile's
`height_levels`):

| Level | Height | In the reference |
|---|---|---|
| -1 | -3 m | the cut floor (rail cut / canal); pits, craters, trenches, basements in general |
| 0 | 0 m | streets, yards, compounds, the junkyard field, the park, the east yard |
| +1 | +3 m | the pipe gallery platform (B2), the ruined factory's upper floor (B13), the low buildings' roofs (not walkable), the cut's rims |
| +2 | +6 m | the ruined factory's roof with its holes |
| +3 | +9 m | the hall / warehouse / Steel Works roofs (walkable; the originals are 8-12 m - game designer 2026-11-02: raised from +2 to +3 "for the view from the roofs") |

The chimneys (B17) are NOT on any level: tall obstacles, never climbable (game designer 2026-11-02).

Links between levels: the three cut ladders (-1 -> 0), L1 (0 -> +1), the ruin's external stair (0 -> +2), its roof holes
(+2 -> +1) and floor holes (+1 -> 0), the stairwells of the halls (0 -> +3, one 9 m flight, 12.9 m run), the two cut crossings
(ramps / bridges at 0 over -1). A ladder over one level = 8 rungs x 36.4 cm + an 8.8 cm plinth under its foot (two levels: 16 rungs + 17.6 cm), so the top
lands exactly on the level (LADDERS.md 3.1). The profile's `zones[].level`, `buildings[].level_top` / `levels_walkable`,
`ladders[].levels` carry the classification; the generator targets `height_levels` 4-5 on the map (-1 / 0 / +1 / +2 / +3 since
2026-11-02) and `route_levels` 2-4 on the mandatory route.

## 2. The map in one paragraph

A long sunken rail cut / canal with concrete retaining walls (trench ~6 m wide, walls 1.5 m thick, ~3.5 m deep, wire fence on
top) runs east-west through the middle (from (0, 67) to (177, 64)) and splits the map in two halves. It is crossed on foot at
two places only - a slab bridge / ramp in the middle by the silos (61, 66) and a footbridge at the NE end (166, 63) - and by
three ladders on its south wall (L2 (6, 70), L3 (43, 70), L4 (125, 68)); a fourth ladder (L1 (12, 21)) is on the far west, at a
pipe gallery / stair building in the north half. NORTH half: office blocks along the NW edge (facades), a pipe yard with the
gallery, the west street with curbs, a fenced compound with a red-roof shed and junk (one gate), the cross street (a T-junction
into the north street), the sawtooth-roof factory hall with a fenced boiler yard, a small office block, the large flat-roof
warehouse with a fenced front lot, the dense junkyard field (40-60 wrecks, fenced, two gaps), and the north street along the
cut's north wall - a 110 m straight lane. SOUTH half: the inner south street along the cut's south wall, the tank farm (two
silos, pipe racks, a long tank, a pump house, fenced lots), a green park (irrelevant), the half-ruined brick factory (sloped
roof with two holes, an annex with holes in its walls, an external stair), the "Steel Works" hall with two chimneys and pipe
racks, the east yard between the hall and the fence, and the south street with jersey barriers and sandbags - the second
110 m lane. Long straight streets and the cut itself = long sightlines (marksman lanes, a killing zone); compounds, yards and
the junkyard = short ones.

## 3. Zones (the profile's `zones`; densities = cover objects per 100 m², my estimate from the image)

| Zone | Kind | Extent (m) | Covers / 100 m² | Sightline (m) | What it exercises |
|---|---|---|---|---|---|
| Z_W_OFFICES | facade | x 0-60, y 0-10 | 0.5 | 20-60 | wall layout along a facade, corner peek at the block ends |
| Z_W_PIPEWORKS | pipe yard | x 0-35, y 10-32 | 3.5 | 8-25 | ladder L1 + elevated vision from the gallery, custom formation among the racks |
| Z_W_STREET | street | x 0-55, y 32-47 | 2.0 | 30-55 | column / diamond on the road, wall layout at the curbs, head-on patrol contact |
| Z_W_COMPOUND | one-entry compound | x 3-50, y 47-62 (gate at (30, 47)) | 3.5 | 8-25 | doorway classification, corner peek at the gate, ambush at the shed corners |
| Z_N_CROSS | T-junction | x 50-66, y 32-60 | 2.5 | 15-40 | the turn from the west street into the north street, far side of a wall at the corner |
| Z_N_SAWTOOTH | industrial hall | x 40-95, y 5-35 | 3.0 | 10-30 | enterable hall + roof (elevated vision), the boiler yard = a one-entry compound (V2) |
| Z_N_OFFICE | block | x 66-95, y 32-50 | 2.0 | 10-30 | facade + doorway, the alley between the office and the warehouse lot (V2) |
| Z_WAREHOUSE | warehouse | x 95-122, y 10-52 | 1.5 | 10-40 | roof overwatch over the north street and the junkyard, a stairwell |
| Z_JUNKYARD | junkyard | x 122-177, y 0-50 (gaps at (123, 48), (170, 50)) | 9.0 | 4-15 | coordinated ambush ground, custom formation, no lanes (close fight) |
| Z_N_STREET | street | x 55-177, y 50-61 | 2.0 | 60-115 | THE marksman lane: column along the cut wall, suppression, wall layout at the curb blocks |
| Z_CUT | cut | x 0-177, y 61-72 | 0.5 | 100-177 | killing zone, crossings = chokepoints, ladders L2-L4, elevated vision from the walls' tops |
| Z_S_INNER_STREET | street | x 55-130, y 72-81 | 1.5 | 40-75 | column along the south wall, corner peek at the chimney base, wall layout at the silo fence |
| Z_TANKS | tank farm | x 45-85, y 78-120 | 3.5 | 10-35 | fenced lots with gates, round covers (silos), the pump house enterable |
| Z_SW_GREEN | open | x 0-45, y 75-124 | 1.0 | 20-45 | open square: diamond / wedge, all-round watch vs ambush |
| Z_RUINED_FACTORY | ruin, two levels | x 88-122, y 80-112 | 2.5 | 6-25 | entered from below AND above, floors with holes, doorways / corners / dead ends inside |
| Z_STEEL_WORKS | industrial hall | x 122-166, y 70-112 | 1.5 | 15-45 | the biggest facade (wall layout + slide), roof overwatch, the chimneys = towers |
| Z_E_YARD | yard | x 164-177, y 58-118 | 2.5 | 20-55 | a narrow yard: column, echelon along the wall |
| Z_S_STREET | street | x 55-166, y 112-121 | 2.5 | 50-110 | the second marksman lane: jersey barriers / sandbags = low covers, ambush from the fence line |

Open areas: Z_SW_GREEN (the park, ~45 x 50 m), the junkyard field (dense, not open ground), the east yard, the warehouse's
front lot, the tank farm's inner lot.

## 4. The route (blue) and its variants

The blue lines form one big loop through both halves plus side pieces. Read in walking order from the green circle:

**Main route R_main, ~365 m** (16 points, the profile's `route.main`): START (1, 38) on the road entering at the west corner ->
east along the west street to (50, 38) -> the T-junction / cross street SE to (63, 56) -> the north street along the cut's
north wall, 100 m east to (165, 57) -> over the NE footbridge, south down the east yard round the Steel Works to (166, 111) ->
west along the south street to (102, 115) -> north into the tank farm (73, 98) -> back out to the south street's SW end
END (62, 116).

| Variant | Length | What it is | Needs |
|---|---|---|---|
| V1_west_gallery | ~39 m | up ladder L1 onto the pipe gallery (4 m): an overwatch over the start and the west street before the squad commits | L1 |
| V2_north_blocks | ~96 m | instead of the cross street straight on: the boiler yard gate, round the small office, the alley between it and the warehouse lot, back to the north street - "the compound / warehouse area" | - |
| V3_through_the_cut | ~77 m | down into the cut at the central crossing, along the trench floor (the killing zone) and out by ladder L4 at the chimney | L4 |
| V4_inner_street | ~131 m | the inner south street along the cut's south wall to the Steel Works' NW corner, the chimney base, into the ruined factory FROM ABOVE (the external stair + roof holes) and out below onto the south street - the designer's "round the Steel Works -> the ruined factory" from the other side | B13 stair |
| V5_ruin_from_below | ~67 m | the ruined factory entered from the south street through the main doors, the annex's wall holes, out west | - |

The designer's description ("west street -> the compound / warehouse area -> along the north side of the cut to the NE end ->
round the Steel Works -> the ruined factory -> back SW along the south street") is R_main with V2 and V4/V5 folded in; the
profile keeps them as variants so the generator can produce layouts whose MANDATORY route is shorter than the designer's full
loop and whose variants add the optional legs.

## 5. The cut and its crossings

| Crossing | Where | Kind |
|---|---|---|
| X_centre | (61, 66) | a slab bridge / ramp, 5 m wide, by the silos - the one ground-level crossing in the middle of the map |
| X_ne | (166, 63) | the footbridge at the NE end (the cut ends 10 m further east) - the route turns south here |
| X_w | (2, 68) | the cut's open west end at the map edge; off the route |

Ladders (yellow marks, each ~3.5-4 m = 10-11 rungs at our 36.4 cm pitch; LADDERS.md: >= 7 rungs or it is decoration):

| Ladder | Where | Connects |
|---|---|---|
| L1 | (12, 21) | the pipe-yard ground <-> the gallery platform B2 (V1's overwatch) |
| L2 | (6, 70) | the cut floor <-> the south bank at the west end (the park) |
| L3 | (43, 70) | the cut floor <-> the south bank by the silos (the tank farm's north fence) |
| L4 | (125, 68) | the cut floor <-> the inner south street at the NW chimney (V3's exit) |

## 6. Buildings (the profile's `buildings`)

| Building | Footprint (m) | Height | Enterable | Roof | Interior / floors |
|---|---|---|---|---|---|
| B1 office blocks (NW edge) | x 0-35, y 0-10 (+ a long block x 35-60) | 8-9 m, tall | no (facades) | no | - |
| B2 pipe gallery | x 8-28, y 12-28 | 4 m platform | yes (open racks) | yes, by L1 | an open steel platform |
| B3 red-roof shed | x 3-18, y 50-62 | 4 m, low | yes, door (18, 56) | no | 1 room |
| B4 sawtooth hall | x 55-95, y 5-32 | 9 m, tall | yes, doors (70, 32), (90, 32) | yes (figures on it) - a stairwell | 1 big floor + a mezzanine |
| B5 boiler yard | x 40-58, y 15-35 | fence 2.5 m | gate (48, 35) | - | boilers = covers |
| B6 small office | x 70-85, y 36-48 | 4.5 m, low | yes, door (78, 48) | no | 1-2 rooms |
| B7 warehouse | x 97-121, y 12-45 | 8 m, tall | yes, doors (97, 30), (110, 45) | yes (figures on it) - a stairwell | 1 open floor |
| B8 warehouse lot | x 95-121, y 45-52 | fence | gate (108, 52) | - | - |
| B9 junkyard fence | x 122-177, y 0-50 | fence | gaps (123, 48), (170, 50) | - | 40-60 wrecks inside |
| B11 silos | x 48-60, y 80-100 | 12 m | no | no | two cylinders dia 8 m (round covers) |
| B12 pump house | x 65-78, y 100-115 | 4.5 m, low | yes, door (65, 108) | no (pipes on it) | 1 room |
| B13 ruined factory, main | x 100-119, y 82-100 | 8 m, tall, 2 floors | yes, doors (100, 95), (112, 100) | yes - an external stair at (108, 100), 2 roof holes | floor 1 with 2 holes down; entered from below AND above |
| B14 ruined factory, annex | x 91-100, y 92-111 | 4 m, low | yes, door + wall holes | no | 1 room |
| B15 Steel Works hall | x 130-162, y 75-108 | 12 m, tall | yes, doors (146, 108), (130, 100) | yes (figures on it) - the pipe stair / a stairwell | one hall + a gallery |
| B16 Steel Works annex | x 122-131, y 95-112 | 5 m, low | no | no | pipe racks |
| B17 chimneys | (121, 72) dia 4 m; (124, 92) dia 3 m | 20-25 m, towers | no | no (not climbable in the original) | tower candidates for us |
| B18 east yard fence | x 164-177, y 58-118 | fence | gaps (166, 58), (166, 118) | - | crates, wrecks |
| B19 south fence | y 119-121 | fence | - | - | - |

Counts: 19 structures, 11 enterable (0.6), 4 walkable roofs (B2, B4, B7, B15) + the ruined factory's upper floor, 4 ladders,
2 stairwells + 1 external stair, 2 towers, ~760 m of walls, ~520 m of fences, 9 buildings / ha.

## 7. Sightlines

| Lane | From -> to | Length | Kind |
|---|---|---|---|
| the north street | (55, 56) -> (177, 55) | 122 m | marksman lane |
| the cut | (0, 67) -> (177, 64) | 177 m | killing zone (seen from both walls' tops) |
| the south street | (55, 116) -> (166, 114) | 111 m | marksman lane |
| the inner south street | (55, 77) -> (130, 76) | 75 m | lane |
| the west street | (0, 39) -> (55, 38) | 55 m | lane |
| the east yard | (168, 58) -> (168, 118) | 60 m | lane |
| the cross street | (58, 32) -> (58, 60) | 28 m | short |

Along the main route the free line of sight is ~25 m at the median, ~75 m at the 90th percentile, 177 m at most; about 45 %
of the route runs through short-sightline zones (compounds, yards, the ruin, the tank farm).

## 8. What each part exercises (our mechanics)

| Mechanic | Where the reference puts it |
|---|---|
| formation column | the north / south streets along the walls, the east yard, the cut floor |
| wedge / diamond | the west street's open stretch, the park, the warehouse lot, the tank farm's inner lot |
| wall layout | every facade: the office blocks, the warehouse's west face, the Steel Works' 33 m north face, the cut's walls |
| corner peek | the T-junction, the compound gate, the shed corners, the chimney base, every alley mouth |
| far side of a wall | the cross street corner, the warehouse lot fence, the ruin's annex |
| custom formation | the pipe yard racks, the junkyard, the tank farm |
| ladders and towers | L1 (gallery), L2-L4 (the cut), the chimneys as towers, the hall roofs |
| elevated vision | the gallery, the warehouse / sawtooth / Steel Works roofs, the cut's walls' tops over the trench |
| patrol contact | the long streets (head-on at range), the cross street (round a corner) |
| ambush | the junkyard, the shed corners, the fence lines of the south street, the ruin's holes |
| coordinated ambush | the north street (flanks from the warehouse lot + the junkyard gap, rear from the cross street) |
| suppression | the two marksman lanes and the cut (the only places long enough) |

## 9. Numbers for the generator (the profile's `targets`)

Route 300-420 m with 3-5 variants; 4-7 ladders; 3-5 walkable roofs; 2-4 stairwells; 6-10 enterable buildings; 2-3 cut
crossings; 2-3 lanes over 80 m; 35-55 % of the route in short-sightline zones; mean cover density 2.0-3.5 per 100 m² (the
junkyard 9, streets 2, compounds 3.5, open 1). Set-pieces: 3 alleys, 3 T-junctions, 5 facades, 4 one-entry compounds, 2 open
squares, 2 cut crossings, 4 ladder overwatches, 2 chimneys (obstacles, not towers - 2026-11-02), 1 ruined two-level factory,
2 dead ends, 1 junkyard field, 2 marksman lanes.

## 10. Open questions for the game designer - ANSWERED 2026-11-02

The answers (the profile's `designer_answers`; LEVEL_GENERATOR.md section 10): 1 the SW dead end stays the end; 2 scale B; 3 the
junkyard stays an ambush field, not a route; 4 the chimneys are NOT ladder towers (no extra height); 5 the canal-floor variant IS
a deliberate kill zone (marked `intent: kill_zone`, treated as intended); 6 the hall roofs go to +3 = 9 m for the view. The
questions as asked:

1. **The END.** The blue loop has no explicit end; the profile ends the mandatory route at the south street's SW end by the
   tanks (62, 116). Right place? (Alternatives: back at the start, the park, the ruined factory.)
2. **Scale.** Scale B (177 x 124 m) recommended; the FOT hex scale A would give 110 x 77 m. Agree with B?
3. **The junkyard.** The route never enters it. Keep it an optional ambush field beside the north street, or route a variant
   through it?
4. **The chimneys as towers** with ladders in our version (the original has none)?
5. **V3 through the cut.** A deliberate killing-zone option for the player, or fenced off so the cut is enemy ground only?
