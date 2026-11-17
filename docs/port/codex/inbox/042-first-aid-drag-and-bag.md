# 042 - First aid: drag = aid on any wounded mate, Shift + drag = give; the medical bag is not visible (Claude, 2026-11-17) - LIGHT-MEDIUM, C++
Branch codex/042-first-aid-drag from the newest main, worktree CodexTactics-agents.
Designer playtest of main (2026-11-17): self-help animations work, but he could NOT give aid to another operative: dragging the Medkit
onto a wounded teammate just TRANSFERS it. He wants: drag onto a WOUNDED teammate (any wound tier, HP below max) = buddy aid; the plain
transfer only with Shift held. He also did not see the new «Медицинский саквояж» (medical bag) anywhere.
Today's rule (docs/port/FIRST_AID_REPORT.md): drag onto a HEAVY-wounded = buddy aid, onto a non-heavy = transfer, Shift = transfer;
the bag: `start_medical_bags` {"MedicSapper": 1} (only the Medic-Sapper starts with one).
Do:
1. Drag rule: dropping a Medkit / Medical bag row on a teammate whose HP < max (any tier) starts buddy aid (the helper = the operative
   whose inventory it is; the existing helper / patient clips, cancel rules unchanged); with Shift held = the old transfer; dropping on a
   teammate at full HP = transfer (nothing to heal). Find the drop handler (grep the inventory drag-drop / OnDrop / FirstAid in UI and
   Characters), quote file:line in the report.
2. The bag: find why the designer does not see it - is it in the Medic-Sapper's start inventory on L_Gen_Depot_1 / generated maps (the
   loadout path the level uses, save game restore, the inventory UI row name / icon)? Fix so the Medic-Sapper visibly has «Medical bag» in
   his inventory row list on a fresh level start (numbers / ids in the existing JSON). If it needs an asset (icon .uasset) - say so, no assets.
3. Smoke: extend FirstAidSmoke - (a) drag a medkit onto a LIGHT-wounded mate -> aid starts (not a transfer); (b) the same with Shift ->
   transfer; (c) the Medic-Sapper's inventory lists the medical bag at level start.
Gate: build, CodexTactics.* first-aid / inventory tests, FirstAidSmoke x2, InventorySmoke, TransferSmoke, typesafe audit --agent codex.
Allowed paths: the first-aid / inventory C++ (name the files in your first chat line before editing), its JSON config, FirstAidSmoke
command file, report. No .uasset / ABP / maps. 10 % rule as always.
