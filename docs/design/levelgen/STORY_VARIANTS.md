# Building story variants - proposal (2026-11-21)

Designer 2026-11-21: every story must also have **preset variants** that can be chosen in advance (forced) - not only the
emergent variation (building geometry, seed, author). This proposal covers the 6 implemented stories (1, 3, 4, 7, 9, 10) on the
new khrushchevka plans (court-side stair cores, 2-4 sections, 3 flats per landing, sections meet only at the roof).
Status: **proposal for the designer**; nothing implemented. Story catalogue: `docs/design/levelgen/gdd/building-stories.md` (branch
claude/building-generator), code `Tools/TrainingGround/src/levelgen/buildingStories.ts`, config `Content/Data/LevelGen/building_stories.json`.

## Data model
- `building_stories.json` gets `variants: { <story>: [ { id, label_ru, params, weight, needs } ] }`.
- A story instance gets `variant: <id>` (+ resolved params). The generator picks a variant by weight among those whose `needs`
  are met by the building (sections, storeys, facade, neighbour); a map / the Training Ground can **force** story + variant per building
  (the showcase map uses this; TG: a "вариант" drop-down next to «другая история»).
- `needs` lists what the building must have (e.g. `sections>=2`, `storeys>=3`, `roof_walkable`, `court_facade`); unmet -> the variant is skipped,
  never rolled into something unreachable (rule 5: every storey reachable without explosives).
- Variants marked **later** need a mechanic that does not exist yet; they stay in the catalogue but are never picked until it lands.

## Variants

### 1 Open
- 1a **plain** (today): entrance doors open, nothing blocked. No other variants (it is the control building).

### 3 Locked door
| id | what | params | needs |
|---|---|---|---|
| 3a | entrance of one section locked, the neighbouring section open; up there and across the roof, down by the hatch | `section` | sections>=2, roof hatches in both cores |
| 3b | all entrances locked, one ground window broken (court) - in through the window | `window_side=court` | window vault (designer's clips 11-22) |
| 3c | entrance locked, kick it (loud, 6 m noise, 3 s) or take 3a / 3b | `kickable=true` | openable / kickable doors (next bgen step) |
| 3d **later** | entrance barricaded from inside - only blow it or another way | `barricade` | destruction P1 (ADR-0004) |

### 4 Rubble on the stair + fire escape
| id | what | params | needs |
|---|---|---|---|
| 4a | rubble on flight 0->1, up by a balcony + fire escape on the street facade (today's rule) | `flight=0, facade=street` | storeys>=2 |
| 4b | rubble on an upper flight (e.g. 2->3); the escape starts from the balcony of the storey below | `flight=k>0` | storeys>=3 |
| 4c | escape on the court facade instead of the street | `facade=court` | court facade free |
| 4d | no escape on this section: go through the neighbouring section and the roof, down by this core's hatch | `escape=roof_link` | sections>=2, roof hatches |

### 7 Roof only from outside
| id | what | params | needs |
|---|---|---|---|
| 7a | facade ladder on the end wall (today's rule) | `facade=end` | roof_walkable |
| 7b | ladder on the court facade, starting from a ground-floor annex / porch roof | `facade=court, start=porch` | porch / annex |
| 7c | ladder starts on a storey-1 balcony (reach the balcony first: rubble / snow / window) | `start=balcony` | balcony, a way onto it |
| 7d **later** | from the roof of a lower neighbour (plank / roof bridge, story 18) | `link=neighbour` | story 18 |

### 9 Broken back windows
| id | what | params | needs |
|---|---|---|---|
| 9a | ground windows of the court facade broken (today's rule) | `side=court, count=all` | window vault for entry |
| 9b | one window only, at the far end (quiet but long walk) | `count=1, pos=end` | - |
| 9c | a storey-1 window above a snow drift / porch roof broken - quiet entry upstairs | `storey=1, under=drift|porch` | drift or porch |
| 9d | windows broken on the end wall | `side=end` | end-wall windows |

### 10 Snow-drift entrance
| id | what | params | needs |
|---|---|---|---|
| 10a | entrance sealed by drift, snow ramp to a storey-1 window on the same facade (today's rule) | `facade=entrance, target=window` | - |
| 10b | the drift reaches a storey-1 balcony instead of a window | `target=balcony` | balcony |
| 10c | drift on the end wall, ramp to an end-wall window | `facade=end` | end-wall window |
| 10d | the whole court side drifted: every entrance sealed, two ramps | `scope=all_entrances` | sections>=2 |

## Questions for the designer
1. Is this list right? Add / remove variants per story.
2. Should a map pick variants randomly by weight (with forcing in the Training Ground), or always force them by hand?
3. Variants 3b / 9a / 9c depend on window vault - fine to ship them now as "visual until the clips land"?
