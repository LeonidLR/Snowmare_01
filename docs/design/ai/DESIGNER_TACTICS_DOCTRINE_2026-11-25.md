# Designer's squad tactics doctrine (from his recorded X6 sector fights, 2026-11-25)

The designer described the patterns he plays by. This is the reference for: squad autonomy / follower behaviour, PMC AI (they should
play the same way against us), the trainer's reward and the demo tagger (Codex 121, Saved/Demos). Order = priority.

1. MOVING - SCAN, NO OPEN FLANKS. While moving every fighter scans (yaw AND pitch - enemies can drop from above); the squad keeps
   every flank watched; he re-aims look points often (Shift-click look-fix). An unwatched flank is a mistake.
2. HIGH GROUND. Take dominant height whenever possible (a block / container, a roof, an upper floor) and look down on the area.
3. ON CONTACT - LEAVE THE LINE OF FIRE FIRST. The first action of any fighter under fire: get out of the line of fire as fast as possible.
   If that is impossible -> drop prone (current behaviour, good) -> CRAWL to the nearest cover -> return fire as soon as possible.
4. COVER THEN OVERWATCH. The first fighter to reach cover immediately covers the others still moving to cover (suppress the threat).
   Take high ground if available.
5. FIGHT = PIN + FLANK. One fighter pins the enemy with frontal fire; another flanks - in stealth or by fast bounds - ideally into the
   enemy's rear. The flanker stays as unseen as possible.
6. APPROACH UNSEEN. Fighters moving toward the enemy minimise exposure (stealth, cover-to-cover, low stance), while the overwatch
   fighter holds the high ground looking down.
Observed PMC behaviour (good, keep): they tried the tunnel under the bridges and used ladders to move unseen.

Uses: G025 scan pitch (1), 121 tagger events (fallback_to_cover_after_contact, high_ground, distract_and_flank; add: prone_then_crawl_to_cover,
first_in_cover_overwatch, flank_into_rear, unwatched_flank_seconds), PMC commander / squad orders, trainer reward terms (later).
