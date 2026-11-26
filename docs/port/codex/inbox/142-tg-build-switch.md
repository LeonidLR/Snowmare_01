# Codex 142 (light; Lead reviews) - Training Ground: choose the build (main / play) for "play sector N" and "open editor" (designer 2026-11-26)

Today Tools/TrainingGround/server/trainingApi.ts launches UnrealEditor only from the TG server's own root (= main): handlePlaySector / handleOpenEditor.
The designer plays in the play copy (../CodexTactics-play, branch play/current, memory play-copy) while agents build in main, so he wants a
switch next to the sector buttons: build = Main | Play (default Play when it is built and fresh, else Main).
- Payload gets `build: 'main' | 'play'`; build root = root or path.join(path.dirname(root), 'CodexTactics-play'). Reject other values (no free paths).
- Guards run on the BUILD root: its Saved/agent.lock, its own running editor (uproject of that root), Lanes rule unchanged. If play is
  missing -> 404 with a Russian message.
- Freshness hint in the UI: reuse Scripts/Tools/editor_status.py logic (or its JSON: Saved/AgentChat/editor_recommend.json + worktree HEAD vs
  built token) to show "Play: собрано, <hash>" / "Play: устарело" next to the switch; remember the last choice (localStorage).
- writeTrainingSelection stays on root (Saved/Training is a junction shared by both checkouts) - verify it with a test.
- Unit tests (trainingApi.test.ts): build=play uses the play uproject + cwd, build=main unchanged, bad build -> 400, play editor running -> 409.
- Branch codex/142-tg-build-switch from origin/main; node tests one file / concurrency 2; dev server on 5180+ and stop it; report
  docs/port/codex/outbox/142-tg-build-switch.report.md; FINISH `142 FINISH | ...`. Queue: right after 139 (designer wants it soon).

URGENT (designer 2026-11-26 13:25): do 142 NOW, before 139 (commit 139 WIP first). He must not launch Play by commands.
Also prove the recordings: a sector fight launched from TG with build=play writes its demo .jsonl into the shared Saved/Demos (junction
CodexTactics-play/Saved/Demos -> main) and TG's fights-per-sector table (129) counts it. Check the other junctions too (Saved/Training,
Content/Maps/KitPilot); report them. UI: the switch must be visible on the sector buttons panel and on "open editor", in Russian.
