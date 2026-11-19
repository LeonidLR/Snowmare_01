# Main PC format: the copy on codex-worker, the day itself, the restore

Designer 2026-11-18: the main PC is formatted completely next week, and the work must not stop. This page covers the copy on
the second machine, the format day and the restore.

- **Copy on the worker:** `C:\Mirror\Main\<source>\` on `codex-worker`. It is a one-way mirror of the main PC, made by
  `Scripts/Tools/mirror_to_worker.py`, with the sources listed in `Scripts/Tools/mirror_sources.json`.
- **Day itself:** the checklist below.
- **Restore:** onto the fresh install.

## 1. The mirror

| What | Where on the main PC | On the worker (`C:\Mirror\Main\`) |
|---|---|---|
| Project working copy (Content packs not in git, uncommitted map edits, `Saved/` with trainer runs, reports, screenshots, backups, `.claude/` project skills) | `Documents/Unreal Projects/CodexTactics` | `CodexTactics` |
| Local-only git commits of every branch | the repository's `.git` | `_git/CodexTactics-local.bundle` + `_git/CodexTactics-branches.txt` |
| Godot archive, source art (ASSETS), docs, MapRefs, videos | `Documents/Codex` | `Codex` |
| Other UE projects | `Documents/Unreal Projects/{BuildingKitLab, PCG_01, WEFALL_Mig_01, _Reference}` | same names |
| Claude Code memory, settings, skills, session transcripts | `~/.claude`, `~/.claude.json` | `dot-claude`, `dot-claude-json` |
| Claude desktop data (side-session notes etc., browser caches excluded) | `~/AppData/Local/Claude-Data` | `Claude-Data` |
| Gemini / Codex / Grok CLI settings and histories | `~/.gemini`, `~/.codex`, `~/.grok` | `dot-gemini`, `dot-codex`, `dot-grok` |
| Git config, SSH host aliases (no keys) | `~/.gitconfig`, `~/.ssh/config` | `gitconfig`, `ssh-config` |
| Machine info: installed apps, user env variable names, git config, scheduled tasks, engines / Marketplace plugins | generated at each sync | `machine-info` |

**Sanitised, not copied as they are:** `.claude.json`, `settings*.json`, `config.toml`, `config.json` and the `mcp*.json`
files go as cleaned copies. Every key / token / oauth / auth / password / session field and every `env` / `headers` block
is replaced by `<removed by mirror_to_worker>`. A file that does not parse is not sent at all.

**Not copied, on purpose:**
- Regenerable folders: `Intermediate`, `Binaries`, `DerivedDataCache`, `Saved/{Logs, Crashes, StagedBuilds, Cooked, Shaders}`, `*.pdb`.
- The tracked repository: it is on GitHub.
- **Secrets.** The script's `GLOBAL_EXCLUDES` drops credential, token, key, password and cookie files, plus the agent chat app's
  Edge profile. They are re-entered by hand (section 3).

**Commands** (main PC, wired link only: the sync refuses a link under 20 Mbit/s, the iPhone hotspot gives ~5):
```
python Scripts/Tools/mirror_to_worker.py plan                    # what would go, nothing sent
python Scripts/Tools/mirror_to_worker.py sync [--only NAME ...]  # send the changes (resumes a cut run)
python Scripts/Tools/mirror_to_worker.py verify                  # the worker's files vs the state (live logs may differ)
python Scripts/Tools/mirror_to_worker.py purge                   # after changing the excludes: delete now-excluded copies on the worker
powershell -ExecutionPolicy Bypass -File Scripts/Tools/mirror_schedule.ps1   # task "CodexTactics Mirror": every 2 h + at logon, hidden
```

**How it behaves:**
- **Changed files:** detected by size + mtime against `%LOCALAPPDATA%\CodexTactics\mirror\<source>.state.json`.
- **Transfer:** tar parts of ~1 GB go over scp and are unpacked on the worker.
- **Deletions:** a file deleted here is moved on the worker to `C:\Mirror\Trash\<date>\` and kept 14 days.
- **Concurrency:** one sync at a time (`sync.lock`).
- **Log:** `%LOCALAPPDATA%\CodexTactics\mirror\sync.log`. The worker's `C:\Mirror\Main\LAST_SYNC.json` holds the time of the last run.

**Measured 2026-11-19** (wired, Tailscale direct; the worker busy with the night trainer):
- link 40-80 Mbit/s;
- the first copy is ~65 GB / 100k files, a few hours;
- later runs send only the changes.

## 2. Format day checklist (main PC)

1. **Agents.** Every agent (Codex / Gemini / Grok / API workers) commits and pushes its branch. List the worktrees with
   `git worktree list` and check `git status` in each.
2. **Designer.**
   - Commit or keep the map edits. The mirror holds them anyway.
   - Close the Unreal Editor.
3. **Last sync.** Stop the agents, then run `python Scripts/Tools/mirror_to_worker.py sync`, then `verify`. Check that
   `LAST_SYNC.json` on the worker shows today and `failed: 0`.
4. **Write down the secrets you will need again** (section 3) **on paper or in a password manager.** The mirror does not
   hold them.
5. **Take the SSH key off the machine yourself** (USB stick / password manager), or plan a new key (section 3).
6. **Format.**

## 3. Secrets and logins to re-enter

`machine-info/user-env-var-NAMES.txt` lists the variable names. Their values are never copied.

| What | Where it goes |
|---|---|
| `ANTHROPIC_API_KEY` and the other API keys (Gemini, xAI ...) | user environment variables |
| SSH key to the worker (`~/.ssh/id_ed25519_worker`) | restore it from your own copy, or make a new key pair and add its public key to the worker's `administrators_authorized_keys` / `authorized_keys` |
| GitHub | `gh auth login`, git credential manager |
| Claude desktop / Claude Code | sign in |
| Gemini CLI / Antigravity, Codex, Grok | each one's own login |
| Tailscale | sign in (same account), check that `codex-worker` is reachable |
| Epic Games Launcher / Fab | sign in, reinstall UE 5.8 + the Marketplace plugins (`machine-info/engines-and-plugins.txt`) |

## 4. Restore on the fresh main PC

1. **Install the apps.**
   - Use `winget import -i machine-info\winget-apps.json`: it reinstalls what winget knows.
   - Install the rest by hand: UE 5.8 (Epic Launcher), Visual Studio 2026 with the C++ game workload, Git LFS, Python 3.10+,
     Tailscale, Claude desktop.
2. **Bring the mirror back.** This needs SSH to the worker (section 3) and the scripts, so clone the repository first:
   `git clone ...` into `Documents/Unreal Projects/CodexTactics`, then `git lfs pull`.
   Then run `python Scripts/Tools/mirror_to_worker.py restore <source> --to <original path>` for each source, starting with
   `CodexTactics`. It is restored on top of the clone: the untracked packs, uncommitted edits and `Saved/` come back.
3. **Local branches.** Run `git fetch C:\path\to\CodexTactics-local.bundle 'refs/heads/*:refs/heads/*'`, using the copy of
   `_git/CodexTactics-local.bundle` brought back.
4. **Agent settings.**
   - Restore `dot-claude` -> `~/.claude`, `dot-claude-json` -> `~/.claude.json`, `dot-gemini` / `dot-codex` / `dot-grok` -> `~/.gemini` etc.
   - Restore `gitconfig` -> `~/.gitconfig` and `ssh-config` -> `~/.ssh/config`.
5. **Build and verify.**
   - Run `Scripts/build.ps1`.
   - Run `Scripts/test.ps1 -Smart`.
   - Open the editor and play the start map.
6. **Re-enable the mirror** with `Scripts/Tools/mirror_schedule.ps1`.

## 5. Work on the worker (the main machine during the gap)

Lead decision 2026-11-19: the mirror stays secret-free. No credential is copied by any script. The designer signs in and
enters the keys on the worker himself.

### 5.1 Installed (2026-11-19, `Saved\Worker\install_tools*.ps1` on the worker, official sources only)

| Tool | How | State 2026-11-19 |
|---|---|---|
| UE 5.8 + VS 18 Build Tools (MSVC 14.51.36231) | earlier setup | present |
| git 2.56 + git lfs 3.8 | earlier setup | present |
| GitHub CLI `gh` | winget `GitHub.cli` | 2.105.0 |
| Node.js LTS | winget `OpenJS.NodeJS.LTS` | 26.13.1 |
| VS Code | winget `Microsoft.VisualStudioCode` | installed |
| Claude desktop | winget `Anthropic.Claude` | installed |
| Claude Code / Gemini CLI / Codex CLI | `npm i -g @anthropic-ai/claude-code @google/gemini-cli @openai/codex` (as on the main PC) | 2.1.328 / 0.69.0 / 0.170.0 |
| Grok CLI | the main PC's own `~/.grok/bin` from the mirror (no download), user PATH | 1.0.88 |
| Visual Studio 2026 Community, game C++ workload (debugging) | winget `Microsoft.VisualStudio.Community` | 18.12.1, finished by the 08:45 reboot (2026-11-19) |
| Blender, MaterialCraft, TextureCraft | winget `BlenderFoundation.Blender`, `ArtCraft.MaterialCraft`, `ArtCraft.TextureCraft` | Blender 5.2.0 and MaterialCraft 0.4.1 (the main PC's versions); TextureCraft 0.6.1, because winget no longer has the main PC's 0.5.0 |

### 5.2 Entered by the designer by hand (names only; the values never pass through any script)

1. **PowerShell script policy** (a security setting, the designer's call). The main PC has `CurrentUser=RemoteSigned`; the
   worker has none, so the npm shims `claude.ps1` / `gemini.ps1` / `codex.ps1` are blocked in PowerShell. Fix it with
   `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`. Until then, use `claude.cmd` / `gemini.cmd` / `codex.cmd`, cmd or Git
   Bash.
2. **User environment variables:** `ANTHROPIC_API_KEY`, `ANTHROPIC_API_KEY_EXTRA`, `GEMINI_API_KEY`, `TYPESAFE_API_KEY`. The
   list comes from `machine-info/user-env-var-NAMES.txt`; the rest there are paths.
3. **GitHub:** `gh auth login`, then `gh auth setup-git`, so that `git push` works.
4. **Git identity:** `git config --global user.name` / `user.email`, as in `C:\Mirror\Main\gitconfig`. It is empty on the worker.
5. **Claude desktop + Claude Code:** sign in.
6. **Codex** (`codex login`), **Gemini CLI** (`gemini`, then sign in or the key), **Grok** (`grok`, then sign in).
7. **Tailscale:** same account. It is already signed in on the worker.
8. **Epic Games Launcher / Fab:** sign in, needed only for new Marketplace content.

### 5.3 Settings and history to bring over (from the mirror, no secrets in it)

- `C:\Mirror\Main\dot-claude\` -> `%USERPROFILE%\.claude\` (memory, skills, settings without env).
- Codex / Gemini / Grok dirs likewise.
- Agent chat: it stores its history in `<checkout>\Saved\AgentChat`, one per machine. Copy
  `C:\Mirror\Main\CodexTactics\Saved\AgentChat` into the worker checkout before agents use the chat there.

### 5.0 Network note (2026-11-19)

After the 08:45 reboot, the worker's Ethernet showed `Disconnected` (cable / port) and it came up on the iPhone hotspot
Wi-Fi. Tailscale then runs over a DERP relay at ~2-5 Mbit/s. In that state the mirror refuses to run (minimum 20 Mbit/s)
and the remote builds are slow. Check the cable before relying on the worker.

### 5.4 Verification checklist (worker-ops runs it and reports pass / fail per line)

| # | Check | Command | 2026-11-19 result |
|---|---|---|---|
| 1 | Tools present | versions of git, git lfs, gh, node, claude.cmd, gemini.cmd, codex.cmd, grok, python, UE 5.8, MSVC | PASS |
| 2 | Build locally on the worker | `worker_sync -SkipSync -SkipBinaries -Build`, which runs the worker's own `Scriptsuild.ps1` | PASS (2026-11-19 08:49, after the reboot with VS 2026) |
| 3 | Smokes locally | the worker's `smoke_parallel` (`-Commands MovementSmoke,CoverSmoke`) | PASS: MovementSmoke PASS, CoverSmoke PASS* (passed on the re-run) |
| 4 | Agent chat runs | `python Scripts\Toolsgent_chat.py read --last 1` | PASS (runs; empty history until 5.3) |
| 5 | git fetch | `git fetch --dry-run origin` | PASS |
| 6 | git push | `git push --dry-run origin HEAD:refs/heads/worker-push-probe` | PASS: the designer asked to move the GitHub credential, and `gh` is logged in |
| 7 | CLI logins | `gh auth status`, `codex login status`, Claude / Gemini / Grok start | gh PASS; API-key env vars moved at the designer's request; the Claude / Codex / Gemini / Grok OAuth logins are the designer's own sign-ins (his choice, not copied) |
| 8 | Editor + playable map | open `CodexTactics.uproject`, play the start map (GTX 1080) | designer, after the logins |

**Expect:**
- A slower editor (i7-6700K, 4 cores) and heavy Lumen on the GTX 1080.
- The test farm and the night trainer stop while the worker is the main machine.
