# Smart Enemies - tactical human squads and monsters

> **Status**: Draft v1 for the designer's review (nothing here is approved; every number marked *proposal* is a starting value)
> **Author**: Claude Fable (design architect session, 2026-11-19), for the lead session
> **Last Updated**: 2026-11-19
> **Implements**: the designer's core requirement of 2026-11-19 (below) + his clip notes 2026-11-18 + his "Crossroads" note 2026-11-19
> **Companions**: [smart-enemies-audit.md](smart-enemies-audit.md) (what exists / is broken, with evidence),
> [smart-enemies-roadmap.md](smart-enemies-roadmap.md) (phases + the first two weeks as tasks)
> **Supersedes as the top-level spec**: docs/design/PMC_DEFENCE.md, TACTICAL_MAP.md sections 3-4, KILL_ZONE_AMBUSH.md and
> PMC_TRAINING_ANALYSIS.md stay valid as detail documents; where they disagree with this GDD, this GDD wins after approval.

Marking used in this document: **[D date]** = a decision the designer already made (binding); *proposal* = my recommendation,
not decided; **OQ-n** = an open question for the designer, listed with alternatives and costs in "Open Questions".

## Overview

Smart Enemies is the system that makes every armed human in the game - PMC "Ronin" fighters, resistance fighters and
survivors - move and fight the way the player's own operatives do: from cover to cover, round corners, up to dominant
heights, into and out of buildings, through windows, as a group with a plan. It also covers the monsters (hounds, zombies,
cutters, brutes), which speak a different behaviour language: packs, scent, fear of fire and suppression, horde pressure.

The system has one job in the game loop: **generate dangerous situations the player can read and out-think**. An enemy is
"smart" when the place he stands, the moment he fires and the way he leaves all make sense to a player watching from the
tactical camera - not when he wins an arena round against a copy of himself.

The designer's requirement (2026-11-19, binding): human enemies must
- cross open ground tactically (bounding, cover to cover, never in the open longer than needed);
- take dominant heights when needed (roofs, upper floors, towers);
- hide behind corners and cover, peek, lean;
- enter buildings when needed, leave them, jump out of windows when needed;
- create MANY dangerous situations: fire from windows, fire from upper floors, go upstairs when the player enters the ground
  floor, ambush, flank, hold chokepoints, fall back through prepared layers.

**The key design statement of this GDD.** These behaviours are *designed manoeuvres*: a group of fighters, in roles, executing
steps on named positions of the terrain. They cannot be reached by tuning the weights of a per-fighter order score, which is
all the night trainer can do today. The system is therefore specified in four parts that must exist in this order:
positions (where it makes sense to stand), movement (how to get there alive), squad plays (who does what, together), and
only then tuning (numbers of the designed plays). Training is the last 10 %, not the engine of behaviour (OQ-2, OQ-10).

## Player Fantasy

"They are soldiers, not targets." The player's three operatives enter a frozen Soviet town held by a professional company
built by their former friend. The enemy was there first and has read the ground: somebody is already behind the crates that
cover the crossroads, somebody watches from the second-floor window, and when the squad breaks for the nearest wall the man
who was waiting behind that wall opens fire. The player survives by reading the same ground faster: noticing the tell,
suppressing the window, sending one man round, taking the roof first, mining the stair.

Three feelings the system must produce:
1. **Respect** - "that was a good position; I would have stood there". The player learns tactics from the enemy.
2. **Readable danger** - before a lethal situation there was something to see or hear, and afterwards the player can say
   what he missed. Never "they shot me through fog from nowhere".
3. **A fight that moves** - enemies press, shift, fall back and come again through the buildings; a firefight is a sequence
   of positions, not two lines trading shots for four minutes.

Monsters give the opposite feeling: not respect but pressure - instinct, numbers, smell and fear of fire - and the best
moments come when the two languages collide (a pack drawn by a PMC firefight; a horde at the door while a marksman pins the
squad inside).

### Pillars

| # | Pillar | Means | Kills |
|---|---|---|---|
| P1 | **Position before fire** | Every human enemy who is not moving is at a firing position in cover; he reached it before contact when he had the time | standing in the open, bunching in a corner, "idle in the crossroads" |
| P2 | **Together, in roles** | A squad acts as hammer / anvil / guard / mover / cover, with crossing fields of fire and spacing | three fighters each doing his own best-scored thing |
| P3 | **The same body, the same rules** | Humans use exactly the operative body and its rules for sight, sound, cover, mantle, ladders, drops, doors, wounds, ammo [D 2026-11-03] | cheating perception, teleports, special enemy-only moves |
| P4 | **Buildings are terrain, not walls** | Windows, floors, stairs, roofs and exits are positions and routes enemies choose between | enemies that only fight on streets; buildings as decoration |
| P5 | **Fair and readable** | Tells before lethal fire, human reaction times, no aimbot, no knowledge the enemy did not earn | "blind deaths", wall-hack tracking, instant head-snaps |
| P6 | **Designed first, tuned second** | A behaviour exists because a play and its test exist; training only moves numbers inside it | hoping tactics emerge from fitness |

## Detailed Rules

### 1. Factions

All three human factions use the shared operative body (section 2) and the same tactical vocabulary. They differ by a
**faction profile** (data, *proposal*: `Content/Data/AI/faction_profiles.json`): which plays they know, how disciplined they
are, what they carry, when they break.

| | PMC "Ronin" | Resistance fighters | Survivors |
|---|---|---|---|
| Canon | exists (NARRATIVE_BIBLE; the company of the 4th friend) | **not in the canon yet - OQ-3** | exist [D 2026-11-18], no bible entry yet - OQ-3 |
| Fantasy | a professional company holding a town | *proposal*: locals who know every yard and hit and run | *proposal*: frightened armed civilians defending one building |
| Plays known | all (section 4) | ambush, bait, flank, fall back, window fire; no layered defence, no breach | hold building, window fire, vertical fallback, escape; never hunt |
| Discipline (fire control) | holds fire until the kill zone; synchronised opening | opens early (shorter patience), breaks contact fast | fires as soon as scared; warning shots first (OQ-3) |
| Equipment | rifle, marksman rifle, shotgun, RPG, sapper charges [D 2026-11-18: sapper = 2 mines + 1 tripwire], medkit | rifle, shotgun, pistol; improvised traps | pistol, shotgun, a hunting rifle; barricades |
| Squad | 3-6, fire teams of 2-3 | 2-4, loose pairs | 1-4, one household |
| Breaks when | *proposal* 67 % casualties, then falls back by layers, never routs | *proposal* 34 % casualties: disengage and vanish | *proposal* first casualty or the building entered: flee or surrender (OQ-18) |
| Relation to monsters | monsters attack PMC too; PMC shoots them only in self-defence (exists: `pmc_operators.json` factions) | OQ-3 | OQ-3 |
| Weapons | only the designer's roster [D 2026-11-08]: rifle now; shotgun -> pistol -> sniper + RPG-7; grenade stays | same roster | same roster |

Only the PMC column is buildable today without new designer decisions. The resistance and survivor columns are proposals that
show how a profile differs; the roadmap builds PMC first and keeps the other two as data on the same code.

Monsters are a separate family (section 7).

### 2. The shared body and the tactical vocabulary

[D 2026-11-03] A human enemy IS an operative (`AOperativeCharacter` on a hostile team). Every skill the player's men have,
enemies have by construction; a new skill is added once, registered in the shared order catalog, and both the player's UI and
the enemy brain can use it.

**Body verbs** (what one fighter can do). "Catalog" = the order exists in `SquadOrderCatalog` so an AI can ask for it; the
audit lists the state of each.

| Verb | Rule for enemies |
|---|---|
| Walk / sprint / stealth walk | The same speeds, noise and stamina as operatives. Sprint only out of sight or under covering fire. |
| Take cover (low / high, slot on a cover object) | One fighter per cover object [D 2026-11-18], >= `min_spacing_m` 5 from a mate. |
| Corner: stand at the edge, peek, lean and fire | The default firing stance at a wall end; the peek exposes only what the player's peek exposes. |
| Stance: stand / crouch / prone | Prone only for long-band weapons at band distance (exists). |
| Mantle / vault (crates, fences, a 95 cm window sill) | Heights only from the MANTLING table; a vault through a `glazed` window breaks it and is loud (opening-states). |
| Ladder | Humans climb; one man on a ladder at a time, mates cover (the player's formation rule R17-1 mirrored). Mutants never climb [D 2026-10-22]. |
| Drop down (drop link, ledge, window) | Controlled drop <= 250 cm (MANTLING); higher = the fall rules with damage / bad landing. See OQ-6. |
| Door: open / close / kick / shotgun breach | By opening state (opening-states GDD); costs time and noise exactly as for the player. |
| Fire: aimed, burst, suppress, hold fire | The operative fire rules; suppression is symmetric [D 2026-10-26]. |
| Reload, switch weapon, sidearm | Exists. |
| Heal self / heal a mate | The first-aid rules; a medic drags nobody yet. |
| Grenade | OQ-12 (PMC carry 0 grenades today). |
| RPG strike | The squad's RPG gunner [D 2026-11-09]. |
| Charges: mine / tripwire | Pre-placed by the level generator around PMC posts [D 2026-11-18 evening]; no runtime laying for now. Attackers spot (3 m), defuse or detour (PMC_DEFENCE D3). |

**Squad verbs** (what a group does; each is a *play*, section 4): bound, hold, take height, garrison a building, fall back
upstairs, escape, enter and clear, ambush, hammer and anvil, bait, flank, hold a chokepoint, fall back by layers, suppress and
move, hunt.

### 3. Layers of control

```
6  AI Director         when / how much: budget, pacing, forced relax        (AI_DIRECTOR.md, not built)
5  Encounter           which squads exist on the level, posture, garrison    (level JSON pmc_squads, generator posts)
4  Squad commander     picks the PLAY for the squad / fire team, gives roles (new layer - the heart of this GDD)
3  Fighter brain       reactive utility INSIDE the role: cover, reload, heal, return fire, relocate (exists)
2  Order catalog       atomic orders shared with the player's UI             (exists, incomplete)
1  Body                AOperativeCharacter skills                            (exists)
0  Terrain knowledge   tactical map + building graph: positions and routes   (exists in part, unvalidated)
```

Rules between the layers:
1. **A fighter never invents a manoeuvre.** Layer 3 may only choose among orders its role allows (a window shooter may duck,
   reload, shift to the next window, leave when the play says so; he may not decide to flank alone).
2. **A play is chosen on events, not every half second**: contact, first shot, casualty, a position lost, the player entering
   the building / floor, a timer, the commander's play finishing. Between events the play runs (commitment).
3. **Knowledge flows up honestly**: a squad knows what its members saw or heard (shared belief with memory, exists) plus what
   its post data says about the terrain. It does not know the player's position otherwise (the "radio intel" cheat of the
   Hunt posture is replaced when sound exists; until then it is listed as a known cheat).
4. **Layer 0 is data, validated at build time**: every firing position used by a play has a checked line of fire from the
   firing stance to the area it claims to cover (section 5).

How the brain of layer 4 is built (utility over plays, a state tree, an HTN) is an architecture decision - OQ-2. This GDD
specifies plays by preconditions, roles, steps and exits so any of them can implement it.

### 4. Squad roles and plays

**Role slots** (assigned by the play, re-assigned only on a casualty or a play change):
hammer (fires into the kill zone), anvil / cut-off (waits on the enemy's predicted retreat), pin (suppresses), mover,
flanker, access guard, sector shooter, window shooter, overwatch (marksman), bait, rear guard, breacher, medic.

**Weapon roles** (who fits which slot): marksman -> overwatch; shotgun -> breacher, access guard, anvil at short range;
rifle -> everything else; RPG gunner -> hammer against a cover cluster; sapper -> his charges are already placed.

**Fire teams.** A squad of 3 is one team. A squad of 4-6 is two teams (the player's SQUAD_SIX_FIRE_TEAMS shapes mirrored):
one team fixes, the other moves; plays below are written per team and combined by the commander.

**Play catalogue.** Each play: when it is chosen, the roles, what the player sees, how it ends.

| Id | Play | Chosen when | Roles and steps | Ends when |
|---|---|---|---|---|
| PL-01 | **Bound** (cover to cover) | any move of more than one bound through ground an enemy may see | mover goes to the next cover <= `bound_max_m`; the others hold a fire line on the threat sector; roles rotate | goal reached; contact -> the play of the moment |
| PL-02 | **Occupy positions** (pre-contact) | a defending squad without contact; a squad that just arrived | every fighter walks to an assigned firing position (section 5) and faces his sector; nobody idles in the open | contact; orders change |
| PL-03 | **Hammer and anvil** (ambush with cut-off) | defending, the enemy's approach known, a kill zone with >= 2 crossing positions and a predicted retreat (F3) | 2 hammers on crossing positions, 1 anvil hidden on the predicted retreat; all hold fire; hammers open together; anvil fires when retreaters reach him | enemy destroyed / broke through / anvil found -> PL-13 or PL-12 |
| PL-04 | **Ambush at the exit** | a chokepoint on the enemy's route (tactical map) | hide at the EXIT, never inside; hold fire until the kill zone (F4); one overwatch above if available | sprung -> fight on; discovered -> PL-12 or relocate |
| PL-05 | **Hold a roof / point** (designer notes 1, 1b) | defending an objective | 1 access guard (stair / hatch: aim at it; outside ladder: roof edge above it, covering ladder and street); the others sector shooters on the approach arc, shifting along the edge | access lost -> PL-11; point empty -> PL-02 |
| PL-06 | **Garrison a building** | a `garrison` / `objective` building (building roles) | window shooters by facade toward the approach, 1 door / stair guard; shooters fire, duck, shift windows after `window_shift_s` or when suppressed | enemy enters the ground floor -> PL-07; building untenable -> PL-08 |
| PL-07 | **Fall back upstairs** | the enemy has entered the storey below / the door is breached | the lowest men break contact up the stair core covered by the stair-head man; the stair head becomes the kill funnel; windows of the upper storey keep the street | upper storey lost -> PL-08; attackers gone for `retake_after_s` -> retake downstairs |
| PL-08 | **Escape** | the building is lost and an exit exists | leave by the exit the enemy does not hold: back door, fire escape, balcony, ground-floor window vault, a drop within the rule (OQ-6); rear guard fires last | all out -> PL-12 toward the next layer or PL-09 from outside |
| PL-09 | **Enter and clear** | the enemy (the player) holds a building the squad must take | stack at a door (DOORWAY_BREACH triad), open / kick / shotgun, clear room by room, stairs by PL-01 on steps; never through a mined door they know | building clear / casualties >= break |
| PL-10 | **Take the height** | a height with a checked field of fire over the fight, an access the squad can guard and an escape (F2 `escape`) | mover(s) climb under cover (PL-01), one stays at the access; a ladder-only roof takes at most 1 man [D 2026-11-17 factor 0.65] | height reached -> overwatch; access threatened -> come down |
| PL-11 | **Suppress and move / flank** | contact, a pinned enemy, a covered flank route (F1) | pin suppresses; flanker takes the route with the lowest exposure, through a building if it is a `through` building; fires from a new angle >= 60 deg | flank fired / route blocked -> back to cover |
| PL-12 | **Fall back by layers** | losing (casualties, flanked, position found) and a prepared rear position exists | half the team fires while half bounds back to the next prepared line (posts of the generator), past its own charges, then roles swap | last layer -> PL-06 in the garrison building or break by the faction profile |
| PL-13 | **Hunt** (sweep) | posture Hunt; the enemy was lost | PL-01 toward the last known place; corners and doors are checked by a peek, not by walking past | contact; search time out -> PL-02 at a good position |
| PL-14 | **Bait and ambush** [D 2026-10-26, postponed 2026-10-28] | resistance / PMC with a hidden team nearby | bait shows himself, fires a few rounds, runs along a route past the hidden team | squad follows -> PL-03; squad does not -> bait returns once |
| PL-15 | **Hold a chokepoint** | defending a narrow passage | nobody stands IN it; positions cover its exit and, when present, its far entrance from above | as PL-04 |

Out of scope for now: deception lures (voices, a false wounded man) [D 2026-10-26 wanted, 2026-10-28 postponed] - OQ-15;
drag-a-wounded-mate; smoke (not in the weapons roster).

### 5. How enemies read the terrain and buildings

**Sources.** (a) The tactical map of a generated level (`tactical/<Map>.tactical.json`: chokepoints, windows with stand
points, overwatch roofs with spots and access, drop spots, hiding spots, ladder-only roofs). (b) The building graph of the
building generator (rooms, storeys, stair cores, ladders, hatches, balconies, fire escapes, openings with states, the building
class through / enterable / decorative and its tactical role objective / garrison / overwatch / transit / background). (c) The
runtime: navmesh, cover slots, line traces, opening states as they change.

**Firing positions** - the unit every play works with. A firing position is a stand point + stance + facing + the area it
covers, and it is valid only when a line of fire from the muzzle at that stance reaches the covered area (checked at build or
load time, not assumed from geometry). Kinds:

| Kind | Stand | Covers | Protection | Notes |
|---|---|---|---|---|
| Corner | wall end, lean side | the street past the corner | the wall | the default street position |
| Low cover | behind a crate / barrier | over the top | crouch | one man per object |
| Window | 60 cm inside, beside the frame, standing | its sector outside (+-75 deg today) | the wall beside the frame; ducks below the sill | state `glazed` breaks on the first shot; `barred` still fires; `boarded` cannot |
| Roof edge | at the parapet, kneeling, stepping to the rim to fire | street below within the weapon band | the parapet (100 cm) | needs the rim step so the parapet does not block the shot (the 2026-11-19 overwatch finding) |
| Stair head | above a flight, off its axis | the flight and its foot | the slab edge / a wall | the indoor chokepoint |
| Door cover | inside a room, off the door line | the doorway | furniture / wall | never opposite the door |
| Balcony / fire-escape landing | on the slab | street, neighbour windows | the rail only (weak) | firing position only unless a fire escape lands there |

**Rules for using a building** (R-B):
- R-B1 **Enter** when: the play's position is inside; or no outdoor cover exists within one bound and a door / broken window
  does; or the building is a `through` building on the best flank route; or to break the line of sight on a retreat.
- R-B2 **Choose the storey** by field of fire: the lowest storey whose window covers the target area inside the weapon band;
  the roof for marksmen. Higher is not automatically better: a higher position has a longer way out.
- R-B3 **Never without an exit**: a position is taken only if it has a second way out the enemy does not hold now (F2
  `escape`), except the last stand of a garrison.
- R-B4 **Go up when they come in** (PL-07): the trigger is honest - a seen enemy on the storey, or a heard door kick, glass
  break, shot or steps inside the building (section 6, hearing).
- R-B5 **Leave** (PL-08) when: enemies on the same storey outnumber the men on it; a grenade / rocket landed in the room; the
  position's sector has been empty for `sector_empty_s` while mates are in contact elsewhere; the play ends.
- R-B6 **Jump out of a window** only as part of PL-08, by the drop rule (OQ-6): ground-floor windows by vault always; an upper
  storey only onto a balcony, a roof, a snow ramp or within the allowed drop.
- R-B7 **Opening states are read as the player reads them**: a `locked` door is an obstacle with a price (kick 3 s, loud), a
  `sealed` one a wall; defenders prefer rooms whose doors the attacker must kick (time to react). Enemies do not change states
  beyond opening / kicking / breaking glass unless OQ-8 says so.
- R-B8 **Breach panels** (building generator S4): defenders cover them like doors; attackers use them only with the RPG gunner.

**Tactical map rules** (kept from TACTICAL_MAP.md 3): chokepoint -> ambush at its exit, never stand in it; window fire ->
windows covering the route; overwatch roof -> the marksman; drop edge -> jumpers (monsters); hiding spot -> ambushers.

### 6. Perception, knowledge and timing (fairness, readability)

1. **Sight**: the operative sight rules, cones, weather and stealth rules, both ways (exists, "honest sight"). A heard shot
   gives a search point, never a target.
2. **Hearing** (*proposal*, needed for R-B4 and for stealth play): enemies hear what the player's actions emit by the same
   radii the opening-states GDD prices - door kick 15 m, glass 10 m, shotgun breach 30 m, gunfire 60 m (exists), sprint steps
   *proposal* 8 m, a mine 60 m. Through a floor / wall the radius is halved (*proposal*).
3. **Reaction time** (F5): a human enemy needs time between seeing and the first aimed shot; longer when surprised, when the
   target is at the edge of his cone, in bad weather. No instant turns: the turn speed is the body's.
4. **Accuracy**: the operative accuracy model, the same spread, falloff, stance and cover modifiers. No hidden bonus. The
   first burst after a turn of more than 90 deg has the snap-shot penalty (*proposal*).
5. **Tells before lethal fire** (P5). Every dangerous situation of section 8 names its tells. Rule: from the first moment a
   careful player could notice the tell to the first lethal volley there are at least `tell_lead_s` seconds on Normal. What a
   first volley may do to an unaware squad is OQ-4.
6. **No spawn in sight, no teleport**: squads appear only out of every operative's sight; an ambusher relocates by a concealed
   walk or not at all [D 2026-11-01, hybrid relocation].
7. **Limited memory**: a lost target is remembered at its last seen place for `contact_memory_s` (exists, 5 s) and searched,
   not tracked through walls.
8. **Readable roles**: the player must be able to tell who is who at the standard zoom - the marksman's long rifle and glint,
   the RPG tube, the sapper's pack; window shooters are visible in the frame when they fire.
9. **Difficulty** changes reaction times, coordination and how many plays a squad chains - not health or damage (OQ-5).

### 7. Monsters - a different language

Monsters do not use plays, cover or weapons. They are specified by **instincts**. The family: Hound, Frostbitten (zombie),
Military Zombie, Cutter, Brute; the zombified Marksman is the bridge (a ranged, ladder-climbing "non-mutant").

| Instinct | Rule | Who | Player counterplay |
|---|---|---|---|
| Pack | Hunt as a group: one pins straight in, others take a real flank detour; fast ones fall back to the pack and come again when morale breaks; brutes and frostbitten never fall back (exists) | hounds, cutters | hold a corner so the flank has one way; kill the pinner first |
| Scent and sound | Track by scent trails and noise, not by sight alone; gunfire draws them from far away | hounds (scent), all (noise) | stealth, wind, cold trails; suppressed movement |
| Fear of fire and suppression | Fire (barrel, campfire, flare) is avoided and rounded; dense fire scatters a pack, melee enemies are not "suppressed" but split round the zone [D 2026-10-26] (exists) | hounds, frostbitten crowds | burning barrels as walls; a suppression zone to split a pack |
| Horde pressure | Many slow bodies on every approach; they never retreat, smash what blocks them, arrive after repeated firefights (exists: horde rules) | frostbitten, military zombies | doors, mantles, height, mines, fire; leave before the horde |
| Drop ambush | Jumpers wait on a roof / wall top above a drop spot and drop when the squad passes (tactical map `drop_spots`) | cutters (and hounds on low drops) | look up, do not hug walls under an edge |
| The heavy | A brute walks through fire and obstacles, draws all attention, opens the way for the rest | brute | focus fire, mines, RPG; do not let it reach the door |
| Buildings | Stairs and ramps: yes (navmesh). Ladders: never for mutants; military zombies and marksmen climb [D 2026-10-22]. Doors: OQ-14 | all | a ladder-only roof or a closed door is safety from mutants, not from humans |

**Collisions between the two languages** (the best situations, section 8 DS-14, DS-17): monsters attack PMC too (exists);
PMC shoot them only in self-defence. A firefight is noise; noise is a dinner bell.

### 8. Catalogue of dangerous situations

Each situation: setup -> enemy behaviour -> tells -> player counterplay -> needs. The first one is the designer's own
acceptance example and the anchor of the whole system.

#### DS-00 Crossroads ambush - hammer and anvil (the designer's acceptance example, 2026-11-19)

The designer on the worst night clip: *"The fighters run for a long time. Then they run up to another group that just stands in
the middle of the street, in the middle of the crossroads, in the open, without taking any cover, not peeking round corners,
not hiding behind crates. A perfect place for an ambush. Instead of spreading out - one behind one cover, the second behind
another, creating crossfire, and the third, understanding that the greens, attacked from above, will retreat down into this
fenced area (like a graveyard), waiting for them already inside it, so the group's death is inevitable - the red team just
stands in the middle of the street and lets the green team run through unhindered."* His sketch: two red positions behind crates
on the far side of the crossroads, left and right, arrows crossing in the open; a third inside the walled enclosure on the
near side, aimed at its entrance gap.

- **Setup**: 3 defenders know the attackers' general direction. An open crossroads; at least two cover objects on its far
  side with a line of fire into it; an enclosure (or any cover area with an entrance) on the side a man under that fire would
  run to.
- **Enemy behaviour** (PL-02 -> PL-03): before contact all three walk to positions - two hammers on separate covers whose
  lines of fire cross in the open, the third (anvil) inside the predicted retreat (F3) with a line on its entrance, hidden
  from the crossroads. All hold fire until the attackers are committed in the kill zone (F4). The hammers open together.
  The anvil fires only when a retreater reaches the entrance.
- **Tells**: a silhouette / weapon over a crate while the hammers wait; footprints in the snow leading behind the crates and
  into the enclosure; the place itself (an open crossroads with cover on the far side reads as danger on the tactical map).
- **Counterplay**: do not cross in a run - bound with one man watching the crates; send one man round; look into the enclosure
  before using it as cover; suppress one hammer and flank the other.
- **Also wrong in the clip (attackers)**: the long run across open ground. With PL-01 the attackers stop at the last cover
  before the crossroads, see or suspect the positions and do not all enter the kill zone.
- **Needs**: firing positions on cover objects with checked lines; crossing-fire selection (F2); retreat prediction (F3); fire
  discipline (F4); PL-02 so nobody idles; PL-01 for the attackers.
- **Acceptance**: section "Acceptance Criteria" A-00.

#### The rest of the catalogue

| Id | Situation | Enemy behaviour (plays) | Tells | Player counterplay | Needs |
|---|---|---|---|---|---|
| DS-01 | **Window fire over the route** | PL-06: riflemen in windows covering the street; fire, duck, shift windows | a figure in the frame, a broken pane, muzzle flash; glazed windows of a garrison are already broken | suppress the window and cross; enter from the blind side; smoke-less: use parked vehicles / fences | window positions with checked sectors; duck / shift; building roles |
| DS-02 | **Upper-floor / roof overwatch** | PL-10 + marksman: long shots down the street, relocates along the edge | scope glint (KILL_ZONE_AMBUSH), the long rifle silhouette on the skyline | stay out of the long rays, climb the neighbour roof, RPG the parapet (later breach) | roof-edge positions that can actually fire; access guard |
| DS-03 | **They go upstairs when you come in** | PL-07: ground floor abandoned, stair head held, upper windows keep firing outside | steps above, a door slammed, voices | mine / tripwire the stair foot, take the fire escape or a neighbour balcony, grenade (OQ-12) | hearing; stair-head positions; storeys in the belief |
| DS-04 | **Ambush at the chokepoint exit** | PL-04 / PL-15 | tracks; a too quiet alley; the HQ radio hint (AI_DIRECTOR) | detour when short; one man through under overwatch | exists in part (tactical map); fire discipline |
| DS-05 | **Crossfire from two buildings** | two fire teams, PL-06 x 2 on facing facades | fire from two sides forces the choice | break into one of the two buildings (it becomes cover against the other) | two-team commander; garrison roles |
| DS-06 | **Bait and ambush** | PL-14 | the bait breaks off too easily; his route passes good cover | do not chase; flank the route | parked until OQ-15 / after PL-03 works |
| DS-07 | **Flank through a building** | PL-11 through a `through` building while a pin fires | suppression from the front with nobody advancing; a door somewhere behind | rear guard, mines on doors behind the squad | through-building routes; exposure cost |
| DS-08 | **Fall back by layers** | PL-12: the fight moves from line to line into the garrison | fire slackens then starts from farther back | do not run after them - their charges lie on the line they left | prepared lines from the generator; own-charge knowledge |
| DS-09 | **Mined approach and doorway** (designer note 2) | generator charges on the direct line and in the door; one defender per cover; tripwire on the flank | disturbed snow; the sapper's spotting rule (3 m) | spot and defuse, detour, or accept the risk | generator posts + charges in game; attackers' mine reaction (D3) |
| DS-10 | **Roof defence with an access guard** (designer notes 1, 1b) | PL-05 | men moving along the parapet | suppress one sector, climb at another; second access | exists on the Depot roof smoke; not on generated maps |
| DS-11 | **They leave through the window and come back** | PL-08 then PL-09 / PL-03 from outside | glass, a body dropping to the snow, tracks | watch the back; do not assume a cleared building stays clear | escape exits in the building graph; OQ-6 |
| DS-12 | **Hunt squad** (meeting engagement) | PL-13 with PL-01 | bounding figures, hand signals / short radio | ambush them (the player plays DS-00 himself) | PL-01 |
| DS-13 | **They storm your building** | PL-09 against the player holding a house | stack at the door, a kick | mine the door, hold the stair head, leave by the window | door runtime (R1); breach hookup |
| DS-14 | **The dinner bell** | a firefight draws a pack / horde onto both sides | howls, the horde arrows | break contact; let the monsters hit the PMC first | noise -> monster attraction; monsters vs PMC (exists) |
| DS-15 | **Drop ambush** (monsters) | jumpers on edges above the route | snow falling from an edge, scratching | look up; walk the middle of the street | drop spots + jumper archetypes |
| DS-16 | **Pack hunt by scent** | hounds follow the trail, split round fire / suppression | howls behind, tracks | fire, wind, a door (OQ-14) | exists |
| DS-17 | **Pinned in a house with the horde at the door** | marksman outside + horde arriving | both tells above | the roof and a second exit; kill the marksman first | DS-02 + horde |
| DS-18 | **RPG on your cover** | RPG gunner on a cover cluster / a window the player fires from | the tube, the back-blast position, a shout | spread out (the game's spacing lesson), move after firing | exists (RpgStrike) |

### 9. What training is for, and when it may run

1. **Training tunes numbers of designed plays** - thresholds, timings, preferences between plays, per faction profile and
   difficulty. It never creates a behaviour. If a behaviour is not seen in a scripted scenario with default numbers, the fix
   is code or data, not a night run.
2. **A training run is allowed only when all of these hold** (the "training gate", checked by a script, *proposal*):
   - G1 every foundation and play smoke of the plays being tuned is green on the training map;
   - G2 **metric liveness**: every metric used by fitness or descriptors is non-zero in the scripted scenario built to
     produce it (a metric that is always 0, like `overwatch_kills` or `guard_share` on 2026-11-19, blocks the run);
   - G3 a baseline is measured: default numbers, fixed seeds, enough rounds to separate candidates (F7);
   - G4 the opponent is not only a copy of itself: at least one leg is a scripted player stand-in (OQ-11);
   - G5 **behaviour floors**: a candidate that drops a designed behaviour below its floor (e.g. time idle in the open, shots
     from assigned positions, crossfire angle) is rejected whatever its fitness.
3. **Fitness measures the player-facing result**, not who wins a mirror match: situation quality (did the designed
   situation happen, readably), pressure without cheating, survival of the enemies as a means, not a goal.
4. **Until the gate passes** the second machine runs scenario regression (many seeds of the scripted situations) instead of
   evolution - OQ-10.
5. Learning Agents (imitation from the designer's recorded play) is a later, separate track - OQ-2 option C.

## Formulas

All constants *proposal* unless marked as existing (`pmc_commander.json`, `tactical_runtime.json`).

**F1 Exposure cost of a move** (PL-01, flank and fall-back routes).
```
exposure(route) = sum over segments i of  len_i / speed_i * seen_i * (1 - cover_i)
  seen_i  = 1 if a known or suspected threat position has a line to the segment, 0.3 if only a tactical-map threat
            (window sector, roof ray, chokepoint) covers it, 0 otherwise
  cover_i = 1 behind cover vs that threat, 0.5 concealed (blizzard, night, smoke later), 0 open
A bound is legal when its own exposure <= bound_exposure_max (3 s) or a mate suppresses the threat or attention-off >= 0.5.
bound_max_m = 12; one mover per fire team; the overwatcher must have a fire line onto the threat sector.
```
**F2 Score of a firing position** for a role.
```
S(p) = fire(p) * protect(p) * escape(p) * (1 - w_danger * danger(p)) * spacing(p) * role_fit(p)
  fire     = share of the target area p's checked line of fire covers inside the weapon band (0 = cannot fire: invalid)
  protect  = cover quality vs the expected threat direction (existing CoverQualityAgainst), +high ground (existing, 0.2 / level)
  escape   = 1 with a second way out not seen from the target area, 0.6 with one way, 0.3 for a ladder-only roof (0.65 existing factor applies to occupancy)
  danger   = tactical-map danger of p (existing DangerAt)
  spacing  = 0 on a mate's cover object, 0.25 within min_spacing_m 5 (existing G008 numbers), else 1
Crossfire for two hammers h1, h2 on kill zone K: angle(h1-K, h2-K) >= crossfire_min_deg 45 (best 60-120) and neither in the other's line of fire.
```
**F3 Predicted retreat** (the anvil of PL-03).
```
For kill zone K and hammers H: candidates = cover areas, enclosures, building entries within retreat_reach_m 25 of K.
R(c) = shield(c vs every h in H) * nearness(c, K) * away(c)        away = 1 when c lies on the side opposite the hammers
The predicted retreat = argmax R, accepted when R >= 0.5.  The anvil position = best F2 position with a line onto the
retreat's entrance or interior, not visible from K, >= anvil_min_m 6 from the entrance.  No candidate -> PL-04 with three hammers.
```
**F4 Opening fire** (ambush discipline; extends the existing `ShouldHoldFire`).
```
open when  attackers_in_K >= min(2, known attackers)  or  lead attacker passed commit_frac 0.5 of K
        or the ambush is discovered (a member seen or shot at)  or  hold exceeded ambush_max_hold_s
Hammers open within sync_s 1.0 of each other.  Distance is NOT contact: an enemy known but outside the position's range
does not end the hold (the 2026-11-18 overwatch finding).
```
**F5 Reaction time.**
```
t_react = t_base(faction, difficulty) * (1 + 0.5 * surprised) * (1 + 0.5 * edge_of_cone) * weather_factor
t_base (Normal): PMC 0.6 s, resistance 0.8 s, survivors 1.2 s          tell_lead_s (Normal) = 2.0
```
**F6 Break.** `casualties / squad_size >= break_frac(faction)` -> the faction's break play (section 1 table).

**F7 Evaluation of a tuned candidate** (training gate G3).
```
accept iff  mean(score_candidate - score_baseline) over N paired seeds > 0 with a one-sided 90 % confidence
            AND every behaviour floor holds;   N >= 30 rounds per scenario (6 rounds per candidate, as on 2026-11-12, is noise)
```

## Edge Cases

- **No valid firing position within reach**: the fighter takes the best cover slot and the play is downgraded (PL-03 -> PL-04
  -> PL-02); he never stands in the open waiting for one.
- **Two fighters want one cover object**: the role with the higher priority takes it; the other takes the next position
  (existing spacing rule), never shares.
- **The predicted retreat is wrong** (attackers break the other way): the anvil counts `anvil_wait_s` after the opening fire,
  then becomes a flanker (PL-11).
- **Contact at long range before the play is set**: distance is not contact (F4); the squad finishes PL-02.
- **A squad of one or two**: plays degrade - hammer and anvil with two = one hammer + anvil; one man = PL-04 alone, then
  breaks by profile.
- **The position's building changes** (door kicked, wall breached, glass broken): positions are re-validated on the event,
  not every tick.
- **A fighter on a ladder or mid-drop when the play changes**: he finishes the climb / drop first (existing rule).
- **A defender's only exit is held**: last stand at the best door-cover / stair-head position; survivors may surrender (OQ-18).
- **PMC and its own charges**: a squad knows its generator-placed charges and never routes over them; a foreign squad's
  charges are spotted by the normal rule.
- **Monsters arrive during a PMC ambush**: self-defence breaks fire discipline; the ambush counts as discovered.
- **Blizzard**: sight halved - plays that need long lines (overwatch) are not chosen; ambush ranges shrink; the squad is
  also without HQ radio, so the Director must not stack a full ambush on a blackout without a tell (AI_DIRECTOR fairness).
- **Turn-based combat**: human enemies are not handled there today - OQ-16.
- **The player uses the same trick** (ambushes a hunt squad): enemies obey the same reaction times and can be destroyed in a
  kill zone; this is intended.
- **Stuck / no path**: after the existing detour steps the fighter gives up the role, reports it, and the commander
  re-assigns; a role that fails twice on a map is a level-data bug to report, not something to hide.

## Dependencies

| System | Why | State (details in the audit) |
|---|---|---|
| Shared operative body, order catalog | every verb | body rich; catalog lacks corner peek, mantle, drop, door, grenade |
| Tactical map (TACTICAL_MAP.md) | chokepoints, windows, roofs, hiding spots | P1-P3 done; positions unvalidated; no building ids |
| Building generator (levelgen GDD set, branch `claude/building-generator`) | storeys, stair cores, windows, exits, roles, opening states | P0 + S1 on the branch, not merged; runtime doors R1 / glass R2 / breach R3 / AI costs R4 not started |
| Level generator PMC posts + charges (task 087) | PL-02 / PL-12 positions and lines | TS done; C++ spawn-at-post and charge ownership not done |
| MANTLING, LADDERS, drop links | vertical movement | body done; no enemy-side proof on generated buildings |
| DOORWAY_BREACH | PL-09 | rules + slots done; no in-game hookup even for the player |
| SUPPRESSION, STEALTH, weapons roster, first aid | fire, hiding, wounds | exist |
| AI Director (AI_DIRECTOR.md) | pacing, when squads activate | draft only |
| PMC arena + night trainer | evaluation and tuning | run but measure the wrong thing (audit) |
| NARRATIVE_BIBLE | resistance and survivors | no entries |
| Clip camera / tactical replay page | the designer's judgement | camera fixed 2026-11-19; replay page planned |

## Tuning Knobs

| Knob | Default | Range | File (*proposal* unless existing) |
|---|---|---|---|
| `bound_max_m`, `bound_exposure_max_s` | 12, 3 | 8-20, 2-5 | `squad_plays.json` |
| `min_spacing_m`, `spacing_penalty` | 5, 0.25 | existing | `pmc_commander.json` (existing) |
| `crossfire_min_deg` | 45 | 30-90 | `squad_plays.json` |
| `retreat_reach_m`, `anvil_min_m`, `anvil_wait_s` | 25, 6, 12 | 15-40, 4-10, 6-20 | `squad_plays.json` |
| `commit_frac`, `sync_s`, `ambush_max_hold_s` | 0.5, 1.0, 20 (existing) | | `squad_plays.json` / existing |
| `window_shift_s`, `sector_empty_s`, `retake_after_s` | 8, 20, 45 | 5-15, 10-40, 20-90 | `squad_plays.json` |
| `t_base`, `tell_lead_s`, `break_frac` per faction and difficulty | F5, F6 | | `faction_profiles.json` |
| hearing radii | section 6.2 | | one table shared with opening-states costs |
| play preferences per faction | 1.0 each | 0-2 | `faction_profiles.json` - the trainer's genes later |
| existing order weights | as is | existing bounds | `pmc_commander.json` (fighter layer only) |

Rule: no number in C++; one JSON read by the game, the Training Ground and the trainer.

## Visual/Audio Requirements

- Tells need assets: scope glint (Niagara), snow tracks toward positions (interactive snow exists), a waiting silhouette
  pose behind cover, window shooter visible in the frame, door slam / steps above / glass / kick sounds (AUDIO_PIPELINE),
  short enemy voice lines for play changes ("fall back", "upstairs", "he's inside") - language and amount OQ-13.
- Readable weapon silhouettes per role at the standard zoom.
- Debug for developers and the designer: `Codex.Pmc.Draw 1` *proposal* - positions, roles, the play name over the squad,
  the kill zone, the predicted retreat; the same data in the arena replay page.

## UI Requirements

- No enemy counts, no contact banners [D 2026-11-01, bug 08].
- Last-known-position ghost for a lost human enemy (a listed follow-up in pmc-squad-ai.md) - recommended with DS-03 / DS-11.
- The tactical-map danger overlay for the player (windows / roofs / chokepoints) is a separate decision of TACTICAL_MAP P5.

## Acceptance Criteria

A behaviour is accepted when (1) its **scenario smoke** passes on fixed seeds, (2) its **situation rate** over 20 seeds on
the lab map is above the floor, and (3) the designer has seen one readable clip of it and said yes. Scenario smokes use a
scripted opponent (a walking / bounding stand-in for the player's squad), not self-play.

**A-00 Crossroads (DS-00)** - the first acceptance example. On the arena layout of the designer's screenshot (crates on the
far side, the walled enclosure on the near side), 3 defenders vs 3 attackers who cross the crossroads:
1. Each defender stands at an assigned firing position at least 5 s before the first attacker enters the kill zone; time
   standing in the open and not moving before contact <= 2 s per man.
2. The two hammers are on different cover objects, >= 5 m apart, crossing angle on the kill zone >= 45 deg.
3. The anvil is inside the enclosure, not visible from the kill zone, with a checked line onto the entrance.
4. No defender fires before F4 allows it; the hammers' first shots are <= 1.5 s apart.
5. Against attackers running straight through (the behaviour of the clip): >= 2 of 3 attackers down in >= 80 % of 20 seeds;
   in >= 60 % of the rounds where an attacker enters the enclosure the anvil hits him there.
6. Against attackers using PL-01: no attacker runs more than `bound_max_m` exposed; fewer than 2 attackers are inside the
   kill zone at the opening fire in >= 60 % of seeds (the bound is the counter - both sides are tested).

| Id | Behaviour | Scenario smoke (*proposed name*) | Pass line | Arena / lab metric |
|---|---|---|---|---|
| A-01 | PL-01 bound | `PmcBoundSmoke` | 40 m of street with 5 covers: every stop in cover; max exposed run <= 12 m; one mover at a time | `max_exposed_run_m`, `open_idle_s` |
| A-02 | PL-02 occupy | `PmcOccupySmoke` | all on positions <= walk time + 3 s; open idle <= 2 s | `open_idle_s`, `position_share` |
| A-03 | Firing positions valid | `FiringPositionSmoke` | 100 % of positions used by plays on the map have a fire line; a marksman on each overwatch roof fires >= 1 shot at a target in range | `shots_from_position`, `overwatch_kills` > 0 |
| A-04 | PL-05 roof defence on generated maps | `PmcRoofDefenceSmoke` on X4 | guard share > 0.6 (not null), spread >= 5 m (existing thresholds) | `guard_share`, `spread_m` |
| A-05 | PL-06 window fire | `PmcWindowFireSmoke` | shooters fire from >= 2 windows, duck when suppressed, shift after `window_shift_s` | `window_positions_used`, `window_shifts` |
| A-06 | PL-07 upstairs | `PmcUpstairsSmoke` | stand-in enters the ground floor: within 6 s nobody of the garrison is on it; the stair head is covered; fire continues from upper windows | `vertical_fallbacks`, `stair_head_hold_s` |
| A-07 | PL-08 escape | `PmcEscapeSmoke` | building lost: every survivor leaves by an exit not held; no fall above the rule | `escapes`, `fall_damage` |
| A-08 | PL-09 enter and clear | `PmcBreachSmoke` | stack, kick / open, every room visited; no walk over a known charge | `rooms_cleared`, `breach_time_s` |
| A-09 | PL-10 take the height | `PmcHeightSmoke` | a man reaches the roof by stair / ladder and fires down; one stays at the access | `height_time_share` |
| A-10 | PL-11 flank | `PmcFlankSmoke` | flank route exposure <= direct route's; shot from >= 60 deg off the pin's line | `flanks_completed` (success, not attempts) |
| A-11 | PL-12 layers | `PmcLayersSmoke` | two bounds back, never both halves moving; no own-charge casualty | `layers_used`, `own_charge_hits` 0 |
| A-12 | Hearing (R-B4) | `PmcHearingSmoke` | a kick at 10 m is heard, at 20 m not; a heard noise is a search point, not a target | - |
| A-13 | Fairness | `PmcFairnessSmoke` | no shot earlier than `t_react` after first sight; no target without sight; no spawn in view | `first_shot_delay_s` |
| A-14 | Monsters (existing) | `HoundScentSmoke`, `SuppressionSmoke`, `HordeSmoke`, `CombatLabAmbushSmoke`, drop jumpers | stay green; new `MonsterDoorSmoke` after OQ-14 | - |
| A-15 | Training gate | `Scripts/Trainer` gate test | a run refuses to start with a dead metric or without a baseline | - |

Whole-system acceptance for the first milestone (*proposal*): on the lab map the designer plays 30 minutes against PMC and
can name, unprompted, at least five different situations of section 8 that happened to him.

## Open Questions

Answered: **which clips were catastrophic** - the Crossroads clip (DS-00) is the first acceptance example; if other clips of
that night showed other failures, each becomes an A-nn line.

| # | Question | Alternatives (cost) | Recommendation |
|---|---|---|---|
| OQ-1 | First milestone scope: which situations first? | (a) DS-00 + DS-01 + DS-03 (street ambush, windows, upstairs: needs buildings, ~4-6 weeks); (b) DS-00 + DS-04 + DS-10 only (streets and roofs, ~2-3 weeks, no interiors); (c) all of section 8 before showing anything (months, no feedback) | (b) first, then (a) as buildings land |
| OQ-2 | How the squad brain is built | (A) keep the flat per-fighter utility, add orders, keep training (cheapest now; it produced 2026-11-19; cannot express roles or sequences); (B) authored squad plays above the existing utility (2-3 weeks for the layer, then ~2-4 days per play; deterministic, testable, tunable); (C) Learning Agents first (experimental plugin, GPU, needs recorded play and a working body anyway; months, opaque) | B; C later as an imitation experiment on top of B |
| OQ-3 | Resistance and survivors: who are they, whose side? | (a) both hostile to the player; (b) resistance fights PMC and may ally with the player, survivors neutral until threatened; (c) decide later - build PMC only, keep profiles as data | (c) now; (b) reads best with the narrative, needs bible entries |
| OQ-4 | What may the first volley of an ambush do to an unaware squad? | (a) full damage after the tell lead (harsh, teaches fast; frustration risk); (b) the first volley wounds but cannot kill (KILL_ZONE_AMBUSH proposal; softer, less respect); (c) by difficulty | (c): Normal = b, Hard = a |
| OQ-5 | Difficulty tiers | (a) one difficulty until the vertical slice (least work); (b) tiers scale reaction, coordination, plays known; (c) tiers scale health / damage / numbers (cheap, feels unfair) | (a) now with (b)'s knobs in data |
| OQ-6 | Window jumps from upper storeys | (a) ground-floor windows only; (b) first upper storey allowed with the fall rule (damage / bad landing) for both sides; (c) a safe scripted jump for enemies (breaks pillar P3) | (b), after the bad-landing task (072) |
| OQ-7 | Squad size in the campaign | (a) 3 as in the arena; (b) 4-6 with two fire teams for garrisons (needs the two-team commander; more readable crossfire, heavier fights); (c) by building role | (c): hunt 3, garrison 4-6 |
| OQ-8 | May defenders change opening states at runtime? | (a) no - only the generator sets states; (b) close and lock doors behind them (needs door runtime R1); (c) barricade at runtime (building-stories Q10; animation + navmesh cost) | (a) now, (b) with R1 |
| OQ-9 | Reinforcements | (a) none: each squad fights alone; (b) the nearest garrison sends a team after T s of fire (Director budget; fights grow); (c) scripted per level | (a) until the Director exists |
| OQ-10 | Night training now | (a) stop evolution until the training gate passes; the worker runs scenario regression on many seeds; (b) keep nightly runs for pipeline health, ignore results (burns the night, tempts conclusions); (c) start the Learning Agents spike on the worker | (a) |
| OQ-11 | Who is the opponent in evaluation? | (a) the playtest bot squad taught to see and fight humans (1-2 tasks; reusable for all tests); (b) replays of the designer's recorded play ("shadow"); (c) self-play only (what failed) | (a) now, (b) when recordings exist |
| OQ-12 | Grenades for human enemies | (a) none; (b) frag only, to flush cover / stairwells, with a shout + visible throw + fuse tell; (c) frag + smoke for crossing (smoke is not in the weapons roster) | (b) after DS-03 exists |
| OQ-13 | Squad leader and voices | (a) no leader, silent squads; (b) a leader whose death delays play changes and lowers break_frac; short voiced calls as tells (audio work) | (b) later; calls are a cheap strong tell |
| OQ-14 | Monsters and doors | (a) closed doors stop mutants; a horde breaks one in T s (doors become a core survival tool); (b) doors do not stop monsters (simpler, loses a tool); (c) only locked doors stop them | (a), with door runtime R1 |
| OQ-15 | Deception lures (voices, false wounded) | (a) stay parked [D 2026-10-28]; (b) one lure type for the resistance after PL-14 | (a) |
| OQ-16 | Turn-based combat and human enemies | (a) real time only for now; (b) TB parity for plays (large: every play needs a TB form) | (a) |
| OQ-17 | Where behaviours are judged | (a) a small hand-built lab map with one fixture per situation (stable, 1-2 days of map work by the designer or the generator session); (b) only generated X4 / X6 (realistic, noisy, slow to read); (c) generator training arenas `L_Gen_Train_1..3` | (a) + (c) |
| OQ-18 | Do humans surrender or flee for good? | (a) fight to the last (simple); (b) broken squads flee the map; (c) survivors can surrender (needs narrative + UI) | (b) for PMC / resistance, (c) later |
| OQ-19 | Cold for enemies (`cold_applies`, pending since 2026-11-03) | (a) cold affects humans like the player's men (garrisons need fires - a tell and a target); (b) enemies ignore cold | (a): the fire is a tell |
| OQ-20 | The Hunt posture's "radio intel" (a deliberate cheat today) | (a) keep until hearing exists; (b) remove now (hunts may never find the player) | (a) |
