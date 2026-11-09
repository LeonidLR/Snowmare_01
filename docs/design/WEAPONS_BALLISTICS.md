> DRAFT by Gemini (read-only run 2026-11-07), NOT reviewed yet - numbers are proposals; verify every code / data claim before use.
# WEAPONS_BALLISTICS.md

---

## 1. The Weapon Data Model

### 1.1 Existing Data vs. New Additions

The runtime source of truth for weapon attributes currently resides in `Content/Data/Weapons/weapons_tuning.json` (applied at startup onto `UWeaponDataAsset` instances such as `DA_Weapon_*` and runtime grenade configurations).

#### Existing Fields in `weapons_tuning.json`
* **`base_damage`** (`float`): Raw base damage dealt to health before armor and distance modifiers.
* **`attack_range_m`** (`float`): Absolute maximum reach (hard range ceiling). No target beyond this distance can be targeted or fired upon.
* **`effective_range_m`** (`float`): Optimal range threshold. Targets within this range receive full hit chance scaling; past this distance, hit chance falls off toward `floor_multiplier` via an exponential curve (`floor_multiplier: 0.1`, `exponent: 2`).
* **`fire_rate`** (`float`): Delay in seconds between consecutive shots/bursts.
* **`armor_penetration`** (`float`, 0.0–1.0): Percentage of target armor mitigated or bypassed.
* **`max_clip_size`** (`int`): Magazine size before a mandatory reload.
* **`default_reserve_ammo`** (`int`): Baseline reserve ammo carried by default (-1 for infinite, e.g., backup pistol).
* **`reload_time`** (`float`): Duration in seconds required to complete a magazine reload.
* **`status_duration`** / **`status_tick_damage`**: Duration and tick damage for status effects (e.g., cryo freeze, fire DoT).
* **`max_range_cells`** (`int`): Grid cell range for legacy/turn-based grid mechanics.
* **`full_auto_interval`** (`float`): Cycling delay between rounds within an automatic burst.
* **`suppression_weight`** (`float`): Relative multiplier for the existing suppression accumulation.
* **`base_hit_chances`** (`array<float>`): Falloff array across discrete distance steps.
* **`distance_damage_multipliers`** (`array<float>`): Raw distance damage falloff per distance band.

#### Proposed Extensions to the Weapon Data Model
To support combat gameplay, armor simulation, and AI engagement bands, the schema is expanded with the following parameters:

```json
{
  "spread_by_stance": {
    "standing": 1.0,
    "crouched": 0.8,
    "prone": 0.5
  },
  "spread_by_movement": {
    "stationary": 1.0,
    "walking": 1.45,
    "sprinting": 2.5
  },
  "recoil": {
    "impulse_pitch_deg": 1.2,
    "impulse_yaw_deg": 0.4,
    "recovery_rate_deg_s": 8.0,
    "continuous_fire_bloom_max": 2.2
  },
  "suppression_power": 18.0,
  "noise_radius_m": 45.0,
  "ballistics_type": "Hitscan",
  "penetration_power_mm": 12.0
}
```

* **`spread_by_stance` (proposal)**: Angular cone multiplier applied to baseline shot dispersion based on the shooter's stance (`Standing`, `Crouched`, `Prone`).
* **`spread_by_movement` (proposal)**: Multiplier applied to spread when the unit is stationary, walking (8-way locomotion), or sprinting.
* **`recoil` (proposal)**:
  * `impulse_pitch_deg`: Vertical camera and aim-point kick per round.
  * `impulse_yaw_deg`: Random horizontal drift per round.
  * `recovery_rate_deg_s`: Angular speed at which the crosshair/aim recovers back to origin.
  * `continuous_fire_bloom_max`: Upper bound multiplier on shot spread during sustained automatic or burst fire.
* **`suppression_power` (proposal)**: Discrete points of suppression added to the target per near-miss or impact (integrating into `docs/design/SUPPRESSION.md`).
* **`noise_radius_m` (proposal)**: Acoustic detection radius in meters where gunshot sound alerts AI perception/breaks patrol state via `AEnemyCharacter::BreakPatrol(Hearing)`.

---

## 2. Ballistics

### 2.1 Hitscan vs. Projectile System

| Weapon | Simulation Type | Trace / Actor Model | Muzzle Velocity (proposal) | Drop / Gravity (proposal) |
|---|---|---|---|---|
| **Beretta Pistol** | Hitscan | Instant LineTraceSingle (`ECC_Visibility`) | N/A | N/A |
| **M16A2 Rifle** | Hitscan | Instant LineTraceSingle (`ECC_Visibility`) | N/A | N/A |
| **Sniper Rifle** | Hitscan | Instant LineTraceSingle (`ECC_Visibility`) | N/A | N/A |
| **Shotgun D-1** | Hitscan (Multi-Ray) | 8 Ray Traces in Cone (`ECC_Visibility`) | N/A | N/A |
| **RPG-7** | Simulated Projectile | Dedicated `ARPG7RocketActor` with sweep | 115 m/s | 9.8 m/s² after 20m |
| **Mortar** | Ballistic Arc | Parabolic ballistic trajectory solver | Variable (Arc peak 35–60m) | Standard Gravity |

### 2.2 Damage Falloff Model
* Current game rule: Hit chance drops past `effective_range_m` down to `floor_multiplier` (0.1) at `attack_range_m` via exponent curve ($1 - 0.9 \cdot t^2$).
* **Damage Falloff (proposal)**:
  * From 0m up to `effective_range_m`: $1.0 \times \text{base\_damage}$.
  * From `effective_range_m` to `attack_range_m`: Linearly scales from $1.0$ down to $0.4 \times \text{base\_damage}$.
  * Beyond `attack_range_m`: Hard cutoff (0% hit chance, no shot permitted by `EvaluateShotLine`).

### 2.3 Shotgun D-1: Pellets and Spread Cone
* **Pellet Count (proposal)**: 8 pellets per shell.
* **Damage Distribution (proposal)**: Base damage 45 split across 8 pellets (~5.625 damage per pellet).
* **Cone Angle (proposal)**: 12-degree full dispersion cone (6-degree half-angle).
* **Resolution**: The shot generates 8 individual traces radiating from the muzzle socket with a pseudo-random Halton distribution within the cone. Allows hitting multiple clustered targets or partially hitting a target partially occluded by low cover.

### 2.4 RPG-7 Rocket Ballistics

> **Superseded by the game designer's numbers (2026-11-09; implemented, `docs/systems/weapons.md` "The RPG-7"):** 115 m/s flat, the fuse armed
> at once (no arming distance, no dud), a direct hit 140 on the target struck, the splash 60 within 4 m on everyone else, class vehicle, no
> back-blast damage and no wall-behind / closed-room / prone refusals; the only refusal is a target closer than 10 m (never inside its own
> blast). The proposal below is kept for history.

* **Flight Velocity (proposal)**: Initial launch velocity of 115 m/s, accelerating to 180 m/s after 0.2s rocket booster ignition.
* **Arming Distance (proposal)**: 10 meters minimum.
  * If impact occurs at $<10$ m, the warhead does not detonate; it deals 35 blunt impact kinetic damage to the target actor without explosive splash.
* **Splash Radius (proposal)**:
  * Inner Lethal Radius ($0 - 2.5$ m): 100% damage (250 damage), guaranteed knockdown.
  * Outer Radius ($2.5 - 6.0$ m): Linear falloff from 100% to 10% damage, knockdown scaled by proximity.
* **Backblast Danger Zone (proposal)**:
  * A 60-degree sector extending 8 meters directly behind the firing operative.
  * Anyone in the backblast sector (friendlies, enemies, or destructible objects) takes 45 points of thermal/kinetic damage and suffers a forced knockdown.
  * Firing with a solid wall/obstacle within 2.0 meters directly behind the shooter reflects backblast onto the shooter, dealing 30 self-inflicted damage.

### 2.5 Mortar Shells (Indirect Fire)
* **Trajectory**: Parabolic high-angle arc calculation ($45^\circ - 85^\circ$ elevation angle). Bypasses all intervening vertical walls, low cover, and obstacles. Roof structures with ceilings intercept mortar shells, causing detonations on the roof level.
* **Range Profile (proposal)**:
  * Minimum Range: 35 meters (dead zone; cannot fire closer than this).
  * Maximum Range: 160 meters.
* **Flight Time (proposal)**: Calculated dynamically via kinematic parabolic equation:
  $$t_{\text{flight}} = \sqrt{\frac{2 \cdot \Delta Z_{\text{apex}}}{g}} + \sqrt{\frac{2 \cdot (\Delta Z_{\text{apex}} - \Delta Z_{\text{target}})}{g}}$$
  Baseline flight times range from 3.5 seconds at 40m up to 7.0 seconds at 150m.
* **Dispersion & Spotter Line of Sight**:
  * **Direct Spotter Present (proposal)**: Target location is within direct line of sight of any living squad operative. Base Circular Error Probable (CEP) dispersion radius is **$\pm 4.0$ meters**.
  * **Blind / Indirect Fire (No Spotter) (proposal)**: Target location is based on radio callout or last-known memory without active LOS. CEP dispersion doubles to **$\pm 12.0$ meters**.

---

## 3. Armour and Penetration

### 3.1 Target Armor Classes

#### Body Armor Classes (Infantry & Mutants)
1. **Class 0: None / Civilian** (Unprotected operatives, basic mutants, hounds). Damage reduction: 0%.
2. **Class 1: Light Vest / Kevlar** (Scout operatives, light PMC). Damage reduction: 25% against small arms (pistols, buckshot); 5% against rifles.
3. **Class 2: Heavy Tactical Plate / E-SAPI** (Heavy PMC, Vanguard operative). Damage reduction: 55% against small arms, 30% against intermediate rifle rounds (5.56mm).
4. **Class 3: Monster Hide / Exoskeleton** (Frostbitten Brute). Damage reduction: 60% resistance against small-arms kinetic fire, 10% against anti-materiel/explosives.

#### Vehicle Armor Classes (APC BTR-82)
Derived from `FVehicleHullConfig` in `docs/systems/vehicles.md`:
* **Frontal Hull Armor**: Armor reduction 80% against small arms ($0.2 \times \text{damage}$), 40% reduction against explosive splash ($0.6 \times \text{damage}$).
* **Side Hull Armor**: Armor reduction 65% against small arms ($0.35 \times \text{damage}$), standard explosive intake ($1.0 \times \text{damage}$).
* **Rear Hull Armor / Engine Grille**: Armor reduction 35% against small arms ($0.65 \times \text{damage}$), vulnerability to heavy anti-tank ($1.25 \times \text{damage}$).
* **Wheels / Running Gear**: Vulnerable hit locations. Takes 100% damage from small arms and blast damage; losing 2 or more wheels reduces max drive speed by 60%.

### 3.2 Penetration Mechanics by Weapon

Penetration modifies the effective damage according to target armor class:
$$\text{Damage Dealt} = \text{Base Damage} \times \left(1.0 - \max(0.0, \text{Armor Reduction} - \text{Armor Penetration})\right)$$

| Weapon | Penetration Value | Class 0 (None) | Class 1 (Light) | Class 2 (Heavy) | Class 3 (Brute) | APC Front | APC Side |
|---|---|---|---|---|---|---|---|
| **Beretta Pistol** | 0.10 (proposal) | 100% | 85% | 55% | 40% | 10% | 15% |
| **Shotgun D-1** | 0.10 (proposal) | 100% | 85% | 55% | 40% | 10% | 15% |
| **M16A2 Rifle** | 0.20 (in data) | 100% | 95% | 75% | 60% | 25% (in data) | 35% |
| **Sniper Rifle** | 0.60 (in data) | 100% | 100% | 100% | 90% | 50% | 60% |
| **RPG-7** | 1.00 (proposal) | 100% | 100% | 100% | 100% | 100% | 125% |
| **Mortar (HE)** | 0.80 (proposal) | 100% | 100% | 100% | 100% | 80% | 100% |

### 3.3 Cover Penetration Rules
When a line-of-sight trace strikes an intervening cover geometry actor (wood barricade, concrete barrier, metal container), a penetration secondary trace continues along the ray:

1. **Light Cover (Wood, Drywall, Thin Sheet Metal)**:
   * Penetrated by: M16A2, Sniper Rifle, RPG-7.
   * Pistol and Shotgun are stopped (0% penetration).
   * Damage reduction through cover: 40% damage penalty to target behind.
2. **Medium Cover (Reinforced Concrete, Sandbags, Masonry Walls)**:
   * Penetrated by: Sniper Rifle (high-power caliber), RPG-7.
   * M16A2, Pistol, Shotgun are completely stopped.
   * Damage reduction through cover: 70% damage penalty to target behind (Sniper); full detonation on outer wall (RPG-7).
3. **Heavy / Armored Cover (APC Wrecks, Industrial Steel Plates, Bunkers)**:
   * Penetrated by: RPG-7 warhead only (destroys cover object if destructible).
   * All small arms and sniper rounds are stopped with ricochet sparks.

---

## 4. New Weapons and Squad Loadouts

### 4.1 New Weapons Specifications

```
                       [WEAPON STAT COMPARISON]
  Range (m)
   160 |--------------------------------------------- Mortar (150m)
   120 |-------------------------------- Sniper (120m)
    80 |------------------------ RPG-7 (80m)
    60 |------------------ M16A2 (60m)
    25 |------- Pistol (25m)
    15 |---- Shotgun D-1 (15m)
       +---------------------------------------------
```

#### Detailed Stat Table

| Parameter | Beretta Pistol | Shotgun D-1 | RPG-7 Launcher | 60mm Light Mortar |
|---|---|---|---|---|
| **Base Damage** | 16 (in data) | 45 (in data) | 250 (proposal) | 180 (proposal) |
| **Splash Damage** | 0 | 0 | 120 (proposal) | 150 (proposal) |
| **Splash Radius** | 0 m | 0 m | 5.0 m (proposal) | 8.0 m (proposal) |
| **Effective Range** | 15 m (in data) | 8 m (in data) | 40 m (proposal) | 120 m (proposal) |
| **Attack Reach (Max)** | 25 m (in data) | 15 m (in data) | 80 m (proposal) | 150 m (proposal) |
| **Fire Rate / Cycle** | 0.45 s (in data) | 1.15 s (in data) | 3.5 s (proposal) | 4.5 s (proposal) |
| **Armor Penetration** | 0.10 (in data) | 0.10 (in data) | 1.00 (proposal) | 0.80 (proposal) |
| **Magazine Size** | 12 (in data) | 8 (in data) | 1 (proposal) | 1 (proposal) |
| **Reserve Ammo** | Infinite (-1) | 40 (in data) | 3 rockets (proposal)| 12 shells (proposal) |
| **Reload Duration** | 1.3 s (in data) | 2.4 s (in data) | 4.2 s (proposal) | 5.0 s (proposal) |
| **Suppression Power** | 6.0 (proposal) | 28.0 (proposal) | 85.0 (proposal) | 100.0 (proposal) |
| **Noise Radius** | 25 m (proposal) | 40 m (proposal) | 90 m (proposal) | 110 m (proposal) |

### 4.2 Mortar Crew Mechanics (Squad of 6)
Operating the heavy 60mm mortar requires dedicated crew roles:
* **Mortar Gunner (Primary Operator)**: Carries the mortar tube assembly (replaces primary weapon; carries secondary pistol). Responsible for deploying, aiming, and firing.
* **Ammo Bearer (Secondary Operator)**: Carries the ammunition crate (capacity: 12 shells).
* **Deployment & Operation Cycle**:
  1. **Deploy Action**: Gunner initiates deploy at a designated location. Deployment duration: **3.5 seconds** (lock to ground, setting baseplate). Unit cannot move while deployed.
  2. **Firing Cycle**:
     * If the Ammo Bearer is within **3.0 meters** of the gunner: Reload time is standard **3.2 seconds**.
     * If the Ammo Bearer is separated or incapacitated: Gunner must reload from their own limited pouch (only 2 ready rounds carried), with reload duration increased to **7.0 seconds**.
  3. **Pack-up Action**: Duration: **2.5 seconds** to return to mobile infantry stance.

### 4.3 Squad of 6 Loadout Roster (Proposal)

| Slot | Role | Primary Weapon | Secondary Weapon | Special Equipment | Tactical Function |
|---|---|---|---|---|---|
| **1** | **Squad Leader** | M16A2 Assault Rifle | Beretta Pistol | Binoculars / Radio Uplink | Mid-range anchor, squad buffs, caller |
| **2** | **Pointman / Scout** | M16A2 Assault Rifle | Beretta Pistol | Smoke Grenades, Medkit | Reconnaissance, point defense |
| **3** | **Marksman** | Sniper Rifle | Beretta Pistol | Spotting Scope | Long-range elimination, anti-perch |
| **4** | **Combat Engineer** | Shotgun D-1 | Beretta Pistol | Repair Kit, Breaching Charges | Close quarters, door breach, APC repair |
| **5** | **Heavy Gunner / AT**| RPG-7 Rocket Launcher| M16A2 Assault Rifle | Spare Rockets (3) | Anti-armor, bunker buster, area burst |
| **6** | **Support Gunner** | 60mm Light Mortar | Beretta Pistol | Mortar Baseplate, Shell Case | Indirect fire, area denial, suppression |

---

## 5. Fairness, Counter-play and the AI Side

### 5.1 AI PMC Engagement Bands

Building upon `pmc_commander.json` and `docs/systems/pmc-squad-ai.md`, AI PMC squads utilize tactical engagement bands to enforce distinct weapon behaviors:

```
[PMC ENGAGEMENT BANDS]
Shotgun D-1  : [4m ===== 14m]
Pistol       :     [10m ===== 25m]
M16A2 Rifle  :          [25m ================= 60m]
RPG-7        :                 [35m ================== 75m]
Sniper Rifle :                        [45m ======================== 110m]
Mortar       :                              [50m ============================== 150m]
```

* **Shotgun PMC (Aggressor)**:
  * Band: 4–14 meters (`close_quarters`).
  * Behavior: Will never engage in standoff; aggressively uses cover bounding (`AdvancePoint`) and sprint-flanking to close within 6 meters before opening fire.
* **M16A2 PMC (Rifleman)**:
  * Band: 25–60 meters (optimal: 19–24m).
  * Behavior: Avoids point-blank combat. If target enters $<15$m, triggers `FallBack` standoff retreat to reset range. Fires from cover corners.
* **RPG-7 PMC (Heavy Specialist)**:
  * Band: 35–75 meters.
  * Targeting Prioritization: Clusters of 2+ operatives, fortified bunker/cover nodes, or player-controlled vehicles (APC).
  * Safety Gate: PMC will not fire if friendly fighters are within the 60-degree backblast sector or within 6.0 meters of the explosion impact zone.
* **Mortar PMC (Artillery Team)**:
  * Band: 50–150 meters.
  * Positioning: Seeks out-of-sight positions (behind warehouses, deep ruins). Relies on forward PMC scouts/marksmen to feed player coordinates.

### 5.2 Counter-Play Design & Fairness Pillars
1. **Telegraphing & Reaction Windows**:
   * **RPG-7**: Visual laser/glint warning and a distinct high-pitched rocket ignition sound 0.4 seconds prior to launch. The rocket flight speed (115 m/s) provides players at 50m approximately 0.43s to dive/crouch.
   * **Mortar**: Distinct high-angle launch "thump" audible across the map, followed by a whistling incoming audio cue 1.5 seconds before impact. Ground decal displays an expanding danger ring, enabling sprinting operatives to evade the center blast.
2. **Exploiting Weapon Weaknesses**:
   * **RPG Backblast Vulnerability**: Flanking an RPG shooter forces them to either draw their secondary weapon or risk blowing themselves up if backed against a wall.
   * **Neutralizing Mortar Spotters**: Killing or blinding (via smoke grenade) the forward PMC spotter immediately degrades enemy mortar accuracy from $\pm 4$m to $\pm 12$m, scattering rounds harmlessly.
   * **Shotgun Range Limit**: Keeping distance $>15$m guarantees complete safety against enemy shotgunners due to the hard `attack_range_m` limit.

---

## 6. Smokes / Unit Tests and Open Questions

### 6.1 Proposed Smokes and Automated Tests

To ensure mathematical parity and prevent regressions, the following tests are defined for `Source/CodexTacticsTests/Private/Combat/`:

1. **`CodexTactics.Combat.Ballistics.FalloffCurve`**:
   * Asserts that M16A2 hit chance is 100% of nominal at $\le 28$m.
   * Asserts exponential decay between 28m and 60m matching `weapons_tuning.json` parameters.
   * Asserts zero shots fired at 60.1m.
2. **`CodexTactics.Combat.Shotgun.PelletDispersion`**:
   * Fires Shotgun D-1 at a static target wall at 8m.
   * Verifies that exactly 8 ray traces are recorded.
   * Verifies that pellet spread conforms to a 12-degree cone with expected standard deviation.
3. **`CodexTactics.Combat.RPG7.ArmingAndBackblast`**:
   * Spawns an RPG-7 shooter facing a target at 6m ($<10$m arming distance). Asserts no explosive splash occurs and kinetic blunt damage is applied.
   * Spawns a target at 30m. Places a test dummy in the rear 60-degree sector at 4m. Asserts dummy takes 45 damage and enters `Knocked_Back` state upon rocket launch.
4. **`CodexTactics.Combat.Mortar.IndirectTrajectoryAndSpotter`**:
   * Places mortar behind a 10m high obstacle with target at 80m. Asserts trajectory clears obstacle.
   * Tests dispersion with an allied spotter having clear sight ($\le 4$m deviation over 50 iterations).
   * Tests dispersion without spotter ($\ge 8$m average deviation).
5. **`CodexTactics.Combat.Penetration.CoverInteraction`**:
   * Traces M16A2, Sniper Rifle, and Shotgun against a 20cm wooden barrier.
   * Asserts Sniper and M16 penetrate with calibrated damage reductions (0% for Shotgun).

### 6.2 Open Questions for the Lead Game Designer

1. **Mortar Squad Economy & Casualties**:
   * If the Ammo Bearer is killed in action, can other squad members dynamically pick up the shell crate, or is the Mortar locked to single-shot slow reloads for the remainder of the encounter?
2. **Friendly Fire on RPG-7 Backblast**:
   * Should the backblast cone inflict full friendly fire damage on human operatives, or should it be soft-capped (e.g., forced knockdown without lethal health reduction) to avoid player frustration in tight urban spaces?
3. **Severe Weather Degradation**:
   * During heavy blizzard conditions (where vision drops to 21m at night), should the Mortar be prohibited from firing entirely without a forward spotter using thermal/radio gear?
4. **APC Integrated Heavy Mortar**:
   * Should a future vehicle variant support a roof-mounted 82mm mortar utilizing the existing vehicle ammo reserve system?
