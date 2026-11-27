# 148 — Fight replay, annotations, decoder and building numbers

Implemented on `codex/148-fight-replay-annotate`, based on `eb5a18ee`. **Code checks pass; screenshot acceptance is blocked.**

## Delivered

- Training Ground **Хроника боя** tab: newest-first recording metadata, round-aware playback, play/pause, 1/2/4x, scrub/event ticks, direct time entry and keyboard transport.
- X6 referenced map geometry, rotated building footprints, stable numbers for all 68 buildings, fences/walls/covers/ladders/stairs, outline sectors, visible units, facing and optional cones, height/cover/stealth markers, orders and 121 combat events/feed.
- World-cm comments, smooth freehand routes, polylines, arrows and circular zones; colour, author, unit, label, optional explicit height; geometry/metadata editing, delete, undo, dirty guards and atomic sidecar save/reload.
- Configurable recording/notes folders. Recording default points to main repo Saved/Demos; this run redirected notes into the lane.
- `Scripts/Tools/demo_notes.py`: short Markdown + JSON, passing sides/distances, rotated building/cover geometry, cover touches, lengths, geometric open-ground crossings and evidence-gated roof zones.
- New building index only; no existing Content JSON, C++, maps, uassets, Config or in-game roof labels changed. In-game labels were explicitly skipped as requested; runtime plumbing would require separate UE work.

Usage and schema: [fight-replay.md](../../../systems/fight-replay.md).

## Checks

- `node --test --test-concurrency=2 src/replay/replay.test.ts` — **7/7 PASS**, one file. Includes exact 121 additive schema/null targets, interpolation across respawns, legacy files, persistence/containment/origin validation and split Cyrillic UTF-8 body chunks.
- `python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v` — **14/14 PASS**, including rotated buildings and covers, side/passing boundaries, sparse routes, corner/endpoint rejection, roof evidence, index completeness.
- `npm run typecheck` — **PASS** after final code changes.
- `npm run build` — **PASS**; existing Vite browser externalization and >500 KB bundle warnings. A later geometry/UTF-8 fix was re-typechecked and its relevant tests rerun.
- All **17 real recordings / 22,213 JSON objects (22,179 timed rows)** parsed; no damaged lines. The available 11-26 files are legacy event evidence; 121 event playback was checked with schema-exact synthetic records read against `gemini/121-recording-tactics` serialization/tagger. No new 121 PIE recording was available.
- Real-recording API integration: list17, map/index68, sidecar save/reload for `2026-11-26_L_Gen_Depot_1_X6_165115.jsonl`; all **17 SHA-256 hashes unchanged** afterwards. Notes wrote only to lane `Saved/Replay148/notes`.
- Requested Sonnet review: [review](148-evidence/sonnet-review.md). Its UTF-8 chunking and cover-yaw findings are fixed; Cyrillic split-chunk regression passes. First response exhausted output thinking without text; the same review was retried once at low effort. Both calls together cost ~$0.16.
- No Unreal build/smokes: no Source or project changes. Port **5180** dev server stopped; listener absence verified.
- Staged Jev boundary audit — **PASS** (19 files, risk0.1/4); `git diff --cached --check` — **PASS**.

## Evidence and screenshot blocker

The Windows Firefox UI loaded the real 11-26 recording and its numbered X6 geometry. The browser tool then stopped with:

> Computer Use has been stopped for this turn because browser URL policy enforcement is not yet supported for the current Windows browser.

UI interaction stopped at that point. **The required 2–3 screenshots showing a UI-drawn route were not captured.** Manual freehand/handle editing and end-to-end browser save/reload remain unverified. No substitute image is presented as a gameplay screenshot. Lead should capture the remaining evidence in a supported browser before accepting this task.

An API-authored validation route against the real recording is committed for review, clearly marked as Codex validation rather than designer instruction:

- [Saved sidecar](148-evidence/2026-11-26_L_Gen_Depot_1_X6_165115.notes.json): t=13:56, round3, `BP_Operative_C_12`.
- [Decoder Markdown](148-evidence/real-recording-notes.md) and [JSON](148-evidence/real-recording-notes.json): route length22 m, building45 EAST side, 1.5 m off its wall.

Reproduce the screenshots: start Vite on 5180+ using the usage doc; select that recording, enter836 seconds, draw a route, save to an isolated notes directory, reload and capture map + route + annotation controls. Stop the server afterwards.

## Limits

Only X6 geometry is supported; unrelated maps error explicitly. Enemies are visible snapshots, not an omniscient reconstruction. Hit lines require recorded actors and use approximate snapshot positions. Decoder reports planar geometry, not navigation/LOS or proved successful tactics. Original recordings and existing content files are untouched.
