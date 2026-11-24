# Codex 114-mechanics-inventory (night, read-only data collection; no code)

Designer 2026-11-24: "the project is bloated, too many mechanics". Core line (memory / decision):
"a squad of 3-6 fighters fights a tactical battle against PMC and monsters, Soviet stagnation-era setting, winter".
The Lead will give the designer ONE page: every mechanic + recommended bucket (core / frozen / parked) + what it costs us.

Collect, per mechanic (group by system: movement & traversal, cover, windows & doors, formation & solo, camera & cutaway, combat &
weapons, PMC AI, monsters & horde, environment (cold / weather / day / snow), vehicles, interactables (mines / C4 / tripwire / turrets /
power / loot), survival (injuries / first aid / prone), stealth & lures, level generator & kit, Training Ground tabs, telemetry /
recording / Learning Agents, tooling):
- one-line what it is; main source folders / files; runtime switch(es) (CVar / LevelJson / data) if any and current default;
- cost: number of unit test filters + smokes mapped to it in Scripts/test_map.json, lines of C++ (rough, cloc-style), bugs / regressions
  touching it in the last 14 days (git log grep), how often it was the cause of a red gate;
- used on which of the 5 kept maps; any dependency on other mechanics (what breaks if it is switched off).
Output: docs/port/codex/outbox/114-mechanics-inventory.md (one table per group, English) + a CSV next to it. No recommendations
needed - the Lead adds them. Read-only: no code, no maps, no worker runs. FINISH line as usual.
