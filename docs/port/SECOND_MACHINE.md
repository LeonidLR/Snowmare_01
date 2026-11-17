# Second machine: `codex-worker`

Background worker for heavy unattended jobs (smoke batches, bot playtests, the night trainer) while the designer plays and
edits on the main PC. Claude drives it from the main PC over **Tailscale + SSH key**; there is no Claude login on it.
Set up 2026-11-17 (plan: memory `second-machine-plan`, 2026-11-11).

## Hardware / software

| | |
|---|---|
| Host name | `codex-worker` (Tailscale MagicDNS; tailnet IP 100.120.94.68) |
| CPU / RAM / GPU | i7-6700K (4C/8T, 2015) / 32 GB / GTX 1080 8 GB, driver 582.78 (Pascal: no new driver branches) |
| Disks | `C:` 2 SSD striped, ~380 GB free at setup (engine, project, DDC) · `D:` HDD (bulky outputs: trainer runs, clips, log archives) |
| Windows | 11 Pro build 26300 (same Insider build as the main PC) |
| Engine | UE 5.8 from the Epic launcher, `C:\Program Files\Epic Games\UE_5.8` (same path as the main PC) |
| Project | `C:\Work\CodexTactics` (clone of `origin/main`) |
| Tools | Git for Windows + Git LFS, Python 3.12 (`%LOCALAPPDATA%\Programs\Python\Python312`, on the user PATH), OpenSSH Server, Tailscale (Run unattended), ffmpeg 9.0.2 (winget `Gyan.FFmpeg`) |
| Not installed | Visual Studio (the worker never compiles), local LLMs (decision 2026-11-15), Claude Code |

Engine patch level may differ between the PCs (worker 5.8.4, main 5.8.3 at setup): that is fine as long as the engine
**BuildId** (`Engine/Binaries/Win64/UnrealEditor.modules`) matches the project binaries; `worker_sync.ps1` checks it and
stops on a mismatch (then update the engine in the launcher on the older side).

## Connection

- Main PC `~/.ssh/config`:
  ```
  Host codex-worker
      HostName codex-worker
      User user
      IdentityFile ~/.ssh/id_ed25519_worker
      IdentitiesOnly yes
      StrictHostKeyChecking accept-new
      ServerAliveInterval 30
  ```
- Key: `~/.ssh/id_ed25519_worker` (main PC, no passphrase, comment `codextactics-main-to-worker`); its public half is in
  `C:\ProgramData\ssh\administrators_authorized_keys` on the worker (`user` is an administrator).
- The worker's sshd default shell is Windows PowerShell 5.1. For anything non-trivial send `powershell -EncodedCommand`
  (what `worker_sync.ps1` does) - nested quotes through ssh break otherwise.
- Check: `ssh codex-worker hostname` -> `codex-worker`. `tailscale ping codex-worker` shows whether the link is direct
  (`via <ip>:41641`, ~1 ms on the university LAN) or relayed (`via DERP(fra)`, ~180 ms: fine for commands, slow for big copies).
- Networks: works across networks (university / home / phone hotspot) through Tailscale. The worker needs its MAC
  registered for the university Ethernet. The iPhone hotspot isolates its clients, so without Tailscale the two PCs do not
  see each other there.

## Daily use (from the main PC)

```
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1                              # sync + MovementSmoke
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -Commands all -Jobs 2        # whole smoke registry
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -SkipSmoke                   # only update code + binaries
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -SkipSync -Commands CoverSmoke
```

What it does:
1. **Code**: `git fetch` + `git reset --hard origin/main` (or `-Ref <commit>`) + `git lfs pull` in `C:\Work\CodexTactics`.
   The designer's untracked packs are never touched (no `git clean`). Nobody edits files on the worker.
2. **Binaries**: copies the changed files of `Binaries\Win64` from this checkout (or `-BinariesFrom <worktree>`) by a
   size + mtime manifest. Build them from the same commit as `-Ref`; the script warns when the commits differ.
   `-NoPdb` skips symbols.
3. **Smokes**: runs `Scripts/smoke_parallel.ps1 -Jobs 2 -ExtraArgs -Multiprocess` on the worker with
   `CODEX_AGENT=worker`, `CODEX_IGNORE_DESIGNER=1` (no designer on that machine).
4. **Results**: `Saved/Logs/Worker/<timestamp>/SmokeParallel-worker.json` + the logs of failed smokes, here on the main PC.

Exit code: 0 pass, 1 a smoke failed, 2 sync / connection error. Long batches: run it as a background command.

## Continuous loop (trainer + smokes) - `worker_loop.ps1`

The worker works around the clock (designer, 2026-11-17). One Windows scheduled task on the worker runs
`Scripts/Tools/worker_loop.ps1`; nothing on it is started by hand any more.

| | |
|---|---|
| Task | `CodexTactics Worker Loop` - S4U as `codex-worker\user`, Highest, no time limit, triggers: at startup (+2 min) and daily `DailyAt`+10 min (watchdog; `IgnoreNew` while it runs), 3 restarts on failure |
| Day cycle | smoke batch (`-SmokeCommands all`, jobs 2) once per day after `-DailyAt` (08:00) -> optional bot batch (`-BotArgs`) -> trainer window `run_night_trainer.ps1 -RunId <RunId> -Parallel 2 -UntilTime 08:00` (the run resumes; clips + `report.html` at the end of the window) -> again |
| Files (worker, `C:\Work\CodexTactics\Saved\Worker\`) | `loop.log`, `loop.status.json` (phase), `loop.state.json` (last smoke batch), `smokes\<stamp>.json/.log`, `sync_points.log`, `trainer-window.log`; trainer: `Saved\Trainer\scheduler.log`, `Saved\Trainer\<RunId>\` |
| ffmpeg | 9.0.2 via `winget install Gyan.FFmpeg` (user PATH through `WinGet\Links`) - the trainer's highlight clips need it; a task started before the install does not see it (restart the task) |

Install / change the run (on the worker; `-Force` re-registers):
```
ssh codex-worker "powershell -ExecutionPolicy Bypass -File C:\Work\CodexTactics\Scripts\Tools\worker_loop.ps1 -Install -RunId pmc-v5 -Start"
```
Watch / control from the main PC:
```
ssh codex-worker "Get-Content C:\Work\CodexTactics\Saved\Worker\loop.log -Tail 20; Get-Content C:\Work\CodexTactics\Saved\Worker\loop.status.json"
ssh codex-worker "Get-Content C:\Work\CodexTactics\Saved\Trainer\pmc-v5\status.json"
ssh codex-worker "New-Item C:\Work\CodexTactics\Saved\Trainer\pmc-v5\STOP"     # restart the trainer (applies synced trainer_config / commander JSON)
ssh codex-worker "New-Item C:\Work\CodexTactics\Saved\Worker\STOP; New-Item C:\Work\CodexTactics\Saved\Trainer\pmc-v5\STOP"   # end the loop
ssh codex-worker "Start-ScheduledTask -TaskName 'CodexTactics Worker Loop'"   # start it again
```

**Sync while it runs** (no stopping the task any more): `worker_sync.ps1` writes `Saved\Worker\SYNC` and
`Saved\Trainer\PAUSE` (text `worker_sync`) on the worker, waits until the running arena wave / smoke batch has finished
(`Saved\agent.lock` free, no engine process; `-WaitEngineMinutes`, default 60 - a full smoke batch takes longer, then it
stops with exit 2 and cleans up), takes `agent.lock` itself (manual, agent `worker-sync`) for git + binaries, appends the
change point to `sync_points.log` (old -> new HEAD, binaries commit, trainer generation / jobs), releases the lock, runs its
own smokes while the trainer is still paused, then removes SYNC and its PAUSE. The paused trainer goes on with the next
wave of the same generation: C++ / map / level data changes apply at once, `trainer_config.json` / `pmc_commander.json`
only after a trainer restart (run STOP above). A SYNC older than 3 h (dead sync, e.g. the SSH link dropped) is removed by
the loop. The old one-shot task `CodexTactics Worker Trainer` (run_night_trainer directly) is disabled.

## First setup (what was done, to repeat on a reinstall)

1. Windows: rename to `codex-worker`, NVIDIA Game Ready driver, power plan "sleep: never".
2. Tailscale: install, log in with the designer's account, **Preferences -> Run unattended**.
   Main PC: the stock installer fails on build 26300 (`CheckServiceDependencies` cannot read the `Dnscache` service
   description: `QueryServiceConfig2` fails for every service on that build). Workaround used: the official MSI with only
   that action removed from `InstallExecuteSequence` / `InstallUISequence` (plus 4 dummy `Property` rows padding the
   string-pool reference count of `NOT Installed`, otherwise the delete blanks other conditions), installed with
   `msiexec /i ... /passive`. Tailscale self-updates may hit the same check; repeat or wait for a Windows fix.
3. OpenSSH Server: `Add-WindowsCapability` silently failed on the hotspot (metered); a script delivered via
   `tailscale file cp` tries it and falls back to `winget install Microsoft.OpenSSH.Preview`, starts sshd (Automatic),
   opens TCP 22, writes `administrators_authorized_keys` (Administrators + SYSTEM only), sets PowerShell as default shell.
4. Epic launcher -> UE 5.8 at the default path, without Starter Content / templates / extra platforms / editor symbols.
   Git for Windows (LFS included), Python 3.12. The first editor start asked for VC++ redist 14.50.35719.0: install the
   engine's bundled one (Microsoft-signed) as admin -
   `"C:\Program Files\Epic Games\UE_5.8\Engine\Extras\Redist\en-us\vc_redist.x64.exe" /install /quiet /norestart`.
5. Repo is public: `GIT_LFS_SKIP_SMUDGE=1 git clone https://github.com/LeonidLR/Snowmare_01.git C:\Work\CodexTactics`.
   LFS objects of the current tree (~2 GB) were copied from the main PC's `.git/lfs/objects` (tar + scp, then
   `git lfs checkout`) instead of downloading them: saves the GitHub LFS bandwidth quota and the phone data.
6. One-off copy (scp) of the designer's untracked `Content/<pack>/` folders (`git status --porcelain Content`, ~10 GB),
   `Config/DefaultScalability.ini` and `Binaries/Win64`. New packs later: copy them the same way
   (`scp -r "<main>/Content/<Pack>" codex-worker:C:/Work/CodexTactics/Content/`).

## Verified 2026-11-17

- The Unreal Editor opens on the worker and shows the map (after the VC++ redist above; first start compiles shaders).
  An open editor locks the project DLLs: `worker_sync.ps1` then stops before copying binaries - close it on the worker.

- `MovementSmoke` (`-nullrhi`): PASS, 144 s (first run on the machine, cold caches); `worker_sync.ps1` end-to-end on a
  full binary set (reset to d85d3a54, 7 changed DLLs / 451 MB copied, MovementSmoke PASS 40 s, JSON back on the main PC).
- `SnowTracksSmoke` (rendered, `-RenderOffscreen`, reads a render target back): PASS, 163 s - D3D12 SM6 on the GTX 1080,
  driver 582.78. Pascal works for rendered smokes run from the SSH (service) session.
- Link: `tailscale ping codex-worker` 1 ms direct on the university LAN; scp of the initial ~14 GB at 4-11 MB/s
  (CPU-bound WireGuard + SSH on the i7-6700K; many small files are slower).

## Risks / notes

- GTX 1080 (Pascal) gets no new driver branches. Most smokes run `-nullrhi`; rendered smokes, clips and the editor UI
  need the GPU - check them first when something only fails on the worker.
- Worker results reflect the copied binaries: a C++ change must be built on the main PC (or an agent worktree with
  `-BinariesFrom`) before the worker can test it.
- The worker has no designer: the designer-priority wait is disabled there; its build lock (`Saved/agent.lock`) is its own.
- Trainer / smokes / bots on the worker: `worker_loop.ps1` (section "Continuous loop"). Outputs still go to
  `C:\Work\CodexTactics\Saved` (C: has room); move run folders to `D:` when C: gets tight.
