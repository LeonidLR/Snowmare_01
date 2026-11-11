# 023 - Late queue (Claude, 2026-11-11 ~18:45) - in order, own branch per item from main, report to claude

1. **Read-only review of Codex's UNCOMMITTED stealth fixes** (he ran out of tokens): MAIN
   Saved/Orchestrator/backup/interim-wip-1111-1703/wip.patch + new.tar (StealthDetectionSmokeCommand.cpp), against
   interim/2026-11-12. The designer's 6 fixes (Codex chat #184-#186): PASSIVE on entry, crawl allowed, confirmed detection cancels
   stealth + shader, works against PMC, enemies fire at the last seen point only, real wall-hug / lean poses from StealthLocomotion
   LeftWall / RightWall / Lean. For each: done / partial / missing (file:line), bugs (trigger, expected / actual). Do not apply the patch.
2. **Second machine setup (scripts + RU guide; nothing is run now).** Target: Windows + UE 5.8.2 launcher build, GTX 1080 8 GB,
   ~32 GB RAM, 2016 CPU, SSD1 125 GB (Windows), SSD2 125 GB (engine + DDC), HDD 1 TB (project, packs, Ollama). Write
   Scripts/SecondMachine/setup.ps1 (idempotent, -WhatIf): git clone + LFS onto the HDD, robocopy of the designer's untracked packs
   from the main PC over Tailscale / a share (list the pack folders from `git status` untracked Content/* here), copy Binaries/ +
   the plugin binaries built on the main PC instead of compiling, Ollama + qwen2.5-coder:7b, OpenSSH server with key-only auth (as a
   documented step the designer confirms - it is a system setting), the trainer task with -Parallel 1. Plus a check script: editor
   opens headless, one smoke passes. Guide: docs/tools/second-machine.md (RU, steps for the designer vs for Claude).
3. Morning: the night trainer review (as planned).
