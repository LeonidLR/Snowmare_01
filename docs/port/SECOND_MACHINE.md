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
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -SkipSmoke                   # only update code + binaries
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -Ref <commit> -SkipSmoke -Frozen   # 16:30 trainer freeze
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -SkipSync -Commands CoverSmoke
powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -SkipSmoke -WithPdb          # + symbols (wired link, crash analysis)
```

What it does:
1. **Code**: `git fetch` + `git reset --hard origin/main` (or `-Ref <commit>`; `-ShipHead` = the `-BinariesFrom`
   checkout's HEAD, unpushed commits travel as a `git bundle` over SSH) + `git lfs pull` in `C:\Work\CodexTactics`.
   The designer's untracked packs are never touched (no `git clean`). Nobody edits files on the worker.
2. **Binaries**: copies the changed files of `Binaries\Win64` from this checkout (or `-BinariesFrom <worktree>`) by a
   size + mtime manifest, **without `*.pdb`** (default since 2026-11-17: ~30 MB per C++ update over Wi-Fi instead of
   ~450 MB; `-WithPdb` adds them). Build them from the same commit as `-Ref`; the script warns when the commits differ.
3. **Smokes**: runs `Scripts/smoke_parallel.ps1 -Jobs 2 -ExtraArgs -Multiprocess` on the worker with
   `CODEX_AGENT=worker`, `CODEX_IGNORE_DESIGNER=1` (no designer on that machine), `CODEX_LOCK_HELD=1`.
4. **Results**: `Saved/Logs/Worker/<timestamp>/SmokeParallel-worker.json` + the logs of failed smokes (`-AllLogs`: every
   log, zipped over), here on the main PC.

Exit code: 0 pass, 1 a smoke failed, 2 sync / connection error / worker busy longer than `-WaitEngineMinutes`.

**Handshake / lock** (any worker_sync run, so the loop, the trainer and several main-PC callers never collide): it writes
`Saved\Worker\SYNC.<token>` and `Saved\Trainer\PAUSE` (text `worker_sync`) on the worker, waits until the running arena
wave / smoke batch has finished, then creates `Saved\agent.lock` **atomically** (manual, agent `worker-sync`, purpose =
token) and holds it for the whole run - git, binaries and its smokes; a second caller waits (`-WaitEngineMinutes`, 60;
the farm uses 20) for it. It appends the change point to `Saved\Worker\sync_points.log` (old -> new HEAD, binaries
commit, trainer generation / jobs; `FROZEN` marks the evening freeze), and at the end removes the lock, its SYNC and -
when no other SYNC is left - the PAUSE. A paused trainer goes on with the next wave of the same generation: C++ / map /
level data changes apply at once, `trainer_config.json` / `pmc_commander.json` only after a trainer restart (run STOP
below). A SYNC older than 3 h (dead run, e.g. the SSH link dropped) is removed by the loop with its lock.

## Test farm 08:00-16:30 - `smoke_parallel.ps1 -Remote auto`

Designer 2026-11-17: agents' smoke batches run on the worker by day, on the main PC otherwise. `Scripts/smoke_parallel.ps1`
has `-Remote auto | on | off` (default: env `CODEX_SMOKE_REMOTE`, else `auto`); `auto` calls
`Scripts/Tools/smoke_remote.ps1`, which sends the batch to the worker when **all** of these hold, otherwise the batch runs
locally as before (one `[smoke_remote] local run: <why>` line):
- the time is 08:00-16:30, or inside a one-off window listed in `%LOCALAPPDATA%\CodexTacticsarm_windows.json`
  (`[{"from": "2026-11-17T19:00", "to": "2026-11-18T00:00"}]`; outside: silently local - trainer time);
- not on the worker itself;
- the worker already holds this checkout's build (`build.ps1 -Remote worker`, matching tree token) - then smokes only;
  otherwise no uncommitted tracked changes under `Source`, `Content`, `Config`, `Plugins`, `Scripts/test_map.json` in the
  calling checkout (the worker gets its HEAD + this checkout's DLLs);
- `Binaries\Win64` exists and `codex-worker` answers SSH within 6 s;
- the worker is free within 20 min (another farm batch / sync), and the sync works.

Remote run: `worker_sync.ps1 -ShipHead -BinariesFrom <this checkout> -AllLogs` (HEAD + DLLs, never compiles there),
jobs <= 2, `-Multiprocess` added; `-Map`, `-PreCmds`, `-RealTime`, `-NoRetry`, `-TimeoutSeconds` pass through. The
logs come back to `Saved\Logs\Smoke-<Name>[-alone].log` and the result JSON to `-ResultJson` (default
`Saved\Logs\SmokeParallel-last.json`, local log paths, plus a `worker` block) - callers see what a local run leaves.
The worker side ignores the designer-priority wait (no designer there); a local run keeps it. No console windows: ssh / scp
/ git run inside the caller's (hidden) console. `-Remote on` = worker or exit 2; `-Remote off` = always local.

## Remote build (second compile slot) - `build.ps1 -Remote worker`

Lead / designer 2026-11-17: agents should not wait 10-30 min in the main PC's single UBT queue. The worker has
**VS Build Tools 2026** (MSVC 14.51.36231 + Windows SDK 10.0.26100 - the same toolchain as the main PC - plus the .NET
Framework 4.8 SDK, which UBT needs for SwarmInterface; installed with `winget install Microsoft.VisualStudio.BuildTools` and
`setup.exe modify --add Microsoft.Net.Component.4.8.SDK`).

```
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Remote worker           # from any checkout / agent worktree
powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands X     # then: smokes run on that very build
```

`Scripts/Tools/build_remote.ps1` (via `worker_sync.ps1 -Build -FetchBinaries`, same lock / handshake as above):
1. ships the checkout's HEAD (unpushed commits as a git bundle), its **uncommitted** tracked changes (`git diff HEAD
   --binary` patch) and its untracked files under `Source` / `Config` (zip); untracked `Source` leftovers of an earlier
   tree are removed on the worker, so the worker's tree equals the caller's;
2. runs the checkout's own `Scripts/build.ps1` on the worker (UBT, 4 parallel actions on the i7-6700K);
3. copies the changed `Binaries\Win64` files back (no `*.pdb`; `build_remote.ps1 -WithPdb` adds them) and the log to
   `Saved\Logs\Build.log` - the same place and summary lines as a local build;
4. writes `Saved\Worker\tree.json` (tree token = HEAD + hash of the uncommitted part, `Scripts/Tools/tree_token.ps1`).
   `smoke_parallel` (-Remote auto / on) compares it with the calling checkout: same tree -> smokes only, no sync and no
   binaries upload; uncommitted work is fine then.

Exit code: 0 built + DLLs here, 1 compile failed, 2 worker / sync / copy error (nothing changed locally: build here).
Timing: `Saved\Logs\RemoteBuilds.jsonl` (caller, door to door) and `Saved\Worker\builds.jsonl` (worker, UBT).
One worker = one extra slot: a second caller waits for the worker lock (30 min) - then build locally.

Measured 2026-11-17 (wired LAN, Development Editor, `CodexTacticsEditor`):

| | worker UBT | door to door (sync + compile + DLLs back) | main PC (lead's figures) |
|---|---|---|---|
| clean (empty Intermediate) | 7.2 min | 8.1 min (214 MB DLLs back) | 8-11 min |
| incremental, one .cpp changed (uncommitted, as a patch) | 35 s | 1.2 min (15 MB back) | 2-5 min |
| smoke on that build (`MovementSmoke`) | 42 s | 0.9 min, no sync | - |

## Continuous loop (test farm + trainer) - `worker_loop.ps1`

One Windows scheduled task on the worker runs `Scripts/Tools/worker_loop.ps1`; nothing on it is started by hand.

| | |
|---|---|
| Task | `CodexTactics Worker Loop` - S4U as `codex-worker\user`, Highest, no time limit, triggers: at startup (+2 min) and daily 08:10 (watchdog; `IgnoreNew` while it runs), 3 restarts on failure |
| 08:00-16:30 test farm | at 08:00 the loop runs the daily subset `Scripts/Tools/worker_daily_smokes.json` (one smoke per core system, ~30-40 min, lead-editable; `-SmokeCommands` overrides), results `Saved\Worker\smokes\`; then it waits (status `farm`) while main-PC builds / smoke batches come in. The worker session posts the subset result in agent-chat `worker-ops` (`--status`; a FAIL as a normal post to claude) |
| 16:30 freeze | the lead posts the frozen commit in agent-chat topic `trainer-night`; the worker session runs `worker_sync.ps1 -Ref <commit> -SkipSmoke -Frozen` (binaries: the main checkout, built from that commit). The loop waits for `Saved\Worker\FROZEN` until 18:00 (`-FreezeWaitMinutes 90`), then trains on the current code anyway (`NO FREEZE` in loop.log) |
| 16:30-08:00 trainer | ONE window `run_night_trainer.ps1 -RunId <RunId> -Parallel 2 -UntilTime 08:00` (the run resumes; clips + `report.html` at the end, so the clips are ready at ~08:15) |
| Files (worker, `C:\Work\CodexTactics\Saved\Worker\`) | `loop.log`, `loop.status.json` (phase), `sync_points.log`, `FROZEN`, `trainer-window.log`, `smokes\` (own batch); trainer: `Saved\Trainer\scheduler.log`, `Saved\Trainer\<RunId>\` |
| ffmpeg | 9.0.2 via `winget install Gyan.FFmpeg` (user PATH through `WinGet\Links`) - the trainer's highlight clips need it; a task started before the install does not see it (restart the task) |

Install / change the run (on the worker; re-registers the task):
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
The old one-shot task `CodexTactics Worker Trainer` (run_night_trainer directly) is disabled.

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
