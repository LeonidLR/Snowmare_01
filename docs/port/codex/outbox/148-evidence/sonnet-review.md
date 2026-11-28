**No data-loss blockers found.** I found two concrete bugs, one medium and one low. I did not run the code or tests; this is from reading the files.

**Checked and correct:**
- The 121 schema handling: `eventUnits` reads who/whom and killer/victim, and `unitsAt` reads `op` for stealth, cover and climb events.
- Multi-round playback: `roundAt` and `frameAt` clip frames to the current round, and interpolation never crosses a round boundary.
- Notes persistence: save is atomic via a temp file and rename, undo and dirty tracking are consistent, and a failed save never touches the recording.
- API containment: leaf-name regex, realpath check, same-origin check for POST, and the recording is only ever read.
- Decoder axes: +X is north and +Y is east, and the cover rotation maths in `demo_notes.py` matches UE yaw.

**Bugs:**

1. **Medium, data corruption.** `server/replayApi.ts`, in the notes POST body loop (`for await (const chunk of req) { body += chunk; ...}`).
   - Appending chunks to a string decodes each chunk as UTF-8 separately.
   - A multi-byte character, such as a Cyrillic label, split across chunk boundaries becomes `U+FFFD` and is saved permanently.
   - Minimal fix: collect the chunks in a `Buffer[]`, check the running byte total, then call `Buffer.concat(chunks).toString('utf8')` before `JSON.parse`.

2. **Low, wrong display, no stored data affected.** `ReplayDepartment.tsx`, in the covers `<rect>` transform (`rotate(${-(c.yaw||0)},…)`).
   - Screen y is −X, so UE yaw going from +X toward +Y is a clockwise, positive SVG rotation.
   - The unit facing lines already use `sin(yaw)` and `-cos(yaw)`, which is the same convention.
   - The covers therefore render mirrored for any yaw other than 0 or 180, while the decoder treats them correctly.
   - Minimal fix: use `rotate(${c.yaw||0},…)`.

One cosmetic note, not worth a fix: a `climb` height event that occurs before the current snapshot falls out of the `e.t >= frame.t` window, so the height reverts until the next climb event.