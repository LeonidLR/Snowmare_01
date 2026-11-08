# Pistol 9mm Custom - optimized source package (2026-11-08)

Source: Fab "Pistol 9mm Custom" by DTry (free Personal licence), `Codex/ASSETS/Weapons/Pistol9mmCustom/extracted` (read-only).
Pipeline: `docs/pipeline/WEAPON_SINGLE_MATERIAL.md`. **Not imported into the game** - this folder is the package for the importer.

## Result

The pack's FBX is a display sheet (4 pistols + every part in a row, 92 808 tris in 230 objects). Per part, from the sheet:

| FBX (`05_Export/`) | Tris before | after | Budget | Notes |
|---|---|---|---|---|
| `SM_Pistol9mm_Pistol` (frame, slide, trigger, sights, pins: 23 objects -> 1) | 10 334 | **3 000** | ~3 000 | sights / trigger / trigger guard / barrel end kept |
| `SM_Pistol9mm_Magazine` | 1 780 | **700** | | seated in the grip |
| `SM_Pistol9mm_Bullet` | 438 | **120** | | standalone cartridge |
| `SM_Pistol9mm_Suppressor1` | 1 414 | **700** | 300-1 500 | |
| `SM_Pistol9mm_Suppressor2` | 1 274 | **599** | | pose estimated |
| `SM_Pistol9mm_Suppressor3` ("Archer" rail housing) | 4 765 | **1 399** | | pose estimated |
| `SM_Pistol9mm_Laser` | 1 728 | **700** | | |
| `SM_Pistol9mm_Flashlight1` (TiON) | 2 970 | **1 100** | | |
| `SM_Pistol9mm_Flashlight2` (Inforce) | 1 882 | **800** | | |
| `SM_Pistol9mm_RedDot` | 4 711 | **1 100** | | lumpiest part; raise `target_tris` to ~1 500 if wanted |
| `SM_Pistol9mm_Picatinny1` | 2 408 | **800** | | |
| `SM_Pistol9mm_Picatinny2` | 1 716 | **900** | | |
| total | 35 420 | **11 918** | | |

Every part is its own mesh with ONE slot `M_Pistol9mm`; all share ONE 1K set (`04_Textures_Baked/`): `T_Pistol9mm_BaseColor` (sRGB),
`T_Pistol9mm_Normal` (DirectX, 16-bit), `T_Pistol9mm_ORM` (linear, R=AO G=Roughness B=Metallic). Source: 13 materials x 5 maps at 4K.
Atlas: the artist's UV islands repacked, 2 px gutters + 6 px dilation, coverage 49 %; texel density pistol 19.4 px/cm,
magazine / bullet / lights / red dot 12.1, suppressors / rails 9.7-10.9. Ray misses 0.12 %, 6.8 % of normal texels clamped (back-face hits).
Renders: `06_Review/Pistol9mm_review_main.png` (pistol side / top / 3-4, before | after), `Pistol9mm_review_parts.png` (every part),
`Pistol9mm_atlas.png`, `Pistol9mm_topdown_scale.png`. Small embossed text (QR, "INFORCE") gets soft at 1K.

## Import notes (verified by importing the FBX into a throw-away scratch project)

- Static meshes, centimetres, import scale 1, no axis change: **muzzle = +X, up = +Z** (UE axes). Pistol length 18.7 cm.
- **Pivot / origin: shared by ALL parts** = grip centre (x), pistol mid plane (y), middle of the grip height (z). Attachments are
  exported in their MOUNTED pose, so attaching every part to the pistol root at identity assembles the weapon. Bullet is the exception:
  standalone, points +X, origin at the case base centre.
- Mount source: the sheet's assembled pistols (rigid fit, rms 0.0001 cm): Magazine, Suppressor1, Laser, Flashlight1, Picatinny1 from
  assembled pistol 1; Flashlight2, RedDot, Picatinny2 from assembled pistol 2 (the red dot sits on Picatinny2). **Suppressor2 and 3 have
  no mounted twin in the sheet: their pose is an estimate** (axis on the muzzle, narrow end rearwards, rear face on suppressor 1's); check
  them when the sockets are made. Suppressor1 / 2 / 3 and Flashlight1 / 2 are alternatives (do not mount together).
- Sockets: muzzle end of the barrel ~ x = +9.3 (just before suppressor 1's rear face), grip = origin. Bounds per part: `05_Export/export_manifest.json`.
- Material: one `M_Pistol9mm` (Opaque, lit): BaseColor sRGB, Normal TC_Normalmap (no Flip Green), ORM TC_Masks sRGB off.
- Object / FBX names: `SM_Pistol9mm_<Part>`; Magazine and Bullet are separate for reload animation. Slide is part of the Pistol mesh (not separate).

## Emissive - report / proposal

The pack has emissive maps for the red-dot reticle (`AIM_SPOT`), both flashlight lenses and the laser block. Not baked (default: none):
glow is a few texels. The red-dot reticle / lens quads (`SPOT` material, 4 tris) were dropped. Proposal: a scalar emissive parameter plus a
small separate lens quad if the dot / beam should glow; no emissive texture.

## Files

`weapon.json` (budgets, weights, pack material -> texture mapping), `01_Source/parts.json` (mount transforms, pivot), `05_Export/export_manifest.json`,
`06_Review/summary.json`. Intermediates are deleted by `cleanup_intermediates.ps1`; the 4K source stays in `Codex/ASSETS`.
