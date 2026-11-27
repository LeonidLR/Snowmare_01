# Training Ground fight replay (task 148)

Open `Tools/TrainingGround`, run `npm ci` once, then:

```powershell
$env:TG_DEMOS_DIR = 'C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics/Saved/Demos'
# Optional: isolate notes when recordings are read-only.
$env:TG_DEMO_NOTES_DIR = 'C:/Work/Lanes/CodexTactics-148/Saved/Replay148/notes'
npm run dev -- --host 127.0.0.1 --port 5180 --strictPort
```

Visit `http://127.0.0.1:5180/?department=replay`, or choose **Хроника боя**.
Default recordings directory is the current user's `Documents/Unreal Projects/CodexTactics/Saved/Demos`.
Default notes directory is the recording directory. Set environment variables before starting Vite.
The same middleware is available in Vite preview; this feature needs its local server.

Choose a recording, scrub or enter a second, play at 1/2/4x. Space toggles playback; arrows move one second (Shift: ten).
Round boundaries stop interpolation across respawns. Visible enemies only are shown; missing observations do not become omniscient positions.
121's `shot_hit` / `damage` use `who,whom`; `kill` / `down` use `killer,victim`; state events use `op`.
Hit lines require both actors' snapshot positions and are approximate. All events remain in the feed even without positions.
Old v1 files show positions, orders and results without inventing confirmed shots, kills or stealth evidence.
Playback length follows game timestamps, since legacy footer duration may use wall time.

Geometry resolves the X6 level's `levelgen_layout` reference. Buildings (including rotation), walls/fences, covers, ladders/stairs and outline sector boxes are drawn.
Other maps fail explicitly rather than display X6 over unrelated recordings. North is +X, east +Y; height is recorded above-ground evidence when available, otherwise raw world Z.
Building numbers are frozen in `Content/Data/LevelJson/L_Gen_Depot_1_X6.buildings_index.json` (68 entries); initial order is centre Y ascending (west/east), centre X descending (north/south), ID tie-break.
Do not regenerate this index during replay. Future geometry changes need an explicit index migration preserving existing IDs/numbers.
The index is excluded from the ordinary level picker. No in-game roof label was added.

## Notes

- Comment: click the map, or **Pin to time only**. Choose a unit optionally.
- Route: drag a freehand route; Catmull-Rom smoothing is sampled to world-cm points before saving.
- Polyline: click vertices, then **Finish polyline**. Arrow/zone: drag. Zone points are centre and radius endpoint.
- Select: click a drawing or its list entry, then edit metadata with **Apply edits**. Drag white handles to edit geometry (up to ~25 handles shown for a sampled route).
- Delete / Undo support 50 recent edits. **Save notes** explicitly persists; department/recording switches and browser close warn about unsaved edits.
- Notes list entries jump to their pinned time. Drawings remain visible at other times, with timestamp labels, for comparison.

`<recording-stem>.notes.json` uses schema `codex.demo_notes/1`, `recording` (JSONL filename), `map`,
`coordinates: UE_cm_X_north_Y_east`, and `annotations[]`:

```json
{"id":"uuid","kind":"route","t":836,"round":3,"author":"Designer","unit":"BP_Operative_C_12","label":"East wall","color":"#facc15","points":[[-4000,3200],[-1800,3200]]}
```

Kinds: `comment`, `route`, `polyline`, `arrow`, `zone`. Optional `height_cm` explicitly identifies annotation height above ground.
Points may contain a third world-Z coordinate. Roof decoding requires explicit height evidence; labels and XY alone are insufficient.
The API validates notes and recording identity, accepts same-origin JSON writes, rejects path traversal/escaping links, caps requests at 4 MB, and atomically replaces notes.
It never opens a recording for writing. Multibyte UTF-8 survives split request chunks.

## Decoder

```powershell
python Scripts/Tools/demo_notes.py <recording-stem>.notes.json
# Optional --map <level-config-or-layout.json> --index <buildings_index.json> --out <output-stem>
python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v
```

Writes `<stem>.md` and `<stem>.json` (default stem `<name>.notes.decoded`). The default X6 config resolves its geometry reference.
For a direct layout input, pass the index explicitly. Decoder rules:

- 25 cm maximum sampling step, including sparse polyline segments; length uses saved/displayed samples.
- Passing means outside a wall, within 5 m and at least `min(200 cm, 25% wall length)` along it; corner-only and perpendicular approaches do not count.
- Rotated footprints are transformed into their building frame for exact distances. Side names use the nearest world-cardinal direction of the wall normal.
- Cover touched means within 75 cm of its rotated footprint; outputs cover ID, kind, position and distance along route.
- Open-ground intervals >=1 m mean >5 m from any building footprint/cover: a planar geometric proximity proxy, without line-of-sight, height or weapon safety claims.
- Footprint intersections are reported with floor/path clearance unknown. A zone centre over a building becomes a confirmed roof only with explicit `height_cm` within 100 cm of its roof height.

This decodes designer intent; it does not validate navigation or infer tactical success. The committed task148 example is a Codex validation annotation, not designer guidance.
