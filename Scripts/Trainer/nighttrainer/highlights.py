"""Highlights: the night's most watchable rounds, as phone-sized mp4 clips and as 2D replays in report.html.

Per night (session = the local date of "now - 12 h", so one 22:00-08:00 window is one session even across restarts):
  - top-N rounds by the arena's per-round `highlight_score` (fallback: kills + flanks + ambushes + closeness),
  - the current champion's most decisive win (largest survivor margin, then the shortest).
Their track files (codex.pmc_arena_track/1) are copied to <run>/highlights/tracks/ (all other per-round track files are
pruned after each generation unless highlights.keep_all_tracks). make_clips() re-runs each round deterministically
(the round's seed, Rounds=1 Record=1, rendered offscreen with CodexTactics.ClipRecorder; frames -> clip_assemble.py ->
ffmpeg) into <run>/highlights/<id>.mp4 (960 px wide, crf ~30, <= max_mb). Mock mode makes placeholder clips with ffmpeg.
"""
from __future__ import annotations

import datetime as _dt
import glob
import os
import shutil
import subprocess
import sys

from .genome import load_json


def session_key(now: _dt.datetime | None = None) -> str:
    return ((now or _dt.datetime.now()) - _dt.timedelta(hours=12)).strftime("%Y-%m-%d")


def fallback_score(row: dict) -> float:
    squad = max(1.0, float(row.get("squad_size", 4)))
    close = 1.0 - abs(float(row.get("survivors", 0)) - float(row.get("opp_survivors", 0))) / squad
    return (float(row.get("kills", 0)) + float(row.get("opp_kills", 0)) + 2.0 * float(row.get("flanks_succeeded", 0))
            + 3.0 * float(row.get("ambushes", 0)) + 3.0 * close)


def caption(row: dict, opponent: str) -> str:
    """One line: who won, what happened, the score in survivors, the length. e.g. "PMC won: flank + suppress, 3-0, 41 s"."""
    other = "Bot squad" if opponent == "bot" else "Champion PMC"
    who = {1.0: "PMC won", 0.0: f"{other} won"}.get(float(row.get("won", 0.5)), "Draw")
    tactics = [name for name, key, need in (("flank", "flanks_succeeded", 1), ("suppress", "suppression_s", 10),
                                            ("ambush", "ambushes", 1), ("retreat", "retreats_survived", 1),
                                            ("heal", "heals", 1))
               if float(row.get(key, 0)) >= need]
    return (f"{who}: {' + '.join(tactics) or 'straight fight'}, {int(row.get('survivors', 0))}-"
            f"{int(row.get('opp_survivors', 0))}, {float(row.get('duration_s', 0)):.0f} s")


def _decisiveness(row: dict) -> float:
    return (float(row.get("survivors", 0)) - float(row.get("opp_survivors", 0))) * 100.0 - float(row.get("duration_s", 0))


def _entry(job: dict, i: int, row: dict, gen: int, score: float) -> dict:
    return {"id": f"{job['id']}_r{i}", "generation": gen, "job_id": job["id"], "round": i,
            "seed": int(row.get("round_seed", int(job["seed"]) + i)), "weights_a": job["weights_a"],
            "weights_b": job.get("weights_b"), "opponent": job["opponent"], "map": job.get("map"),
            "time_limit_s": job.get("time_limit_s"), "duration_s": float(row.get("duration_s", 0)),
            "won": float(row.get("won", 0)), "score": round(score, 3), "caption": caption(row, job["opponent"]),
            "source_track": row.get("track"), "track": None, "clip": None}


def update(run_dir: str, cfg: dict, state: dict, gen: int, jobs: list, results: dict, champion_id: str,
           champion_prefix: str) -> None:
    """Merges this generation's rounds into the session's highlight set and copies the kept track files."""
    key = session_key()
    hl = state.get("highlights")
    if not hl or hl.get("session") != key:
        hl = state["highlights"] = {"session": key, "top": [], "champion_win": None}
    fresh, best_win = [], None
    for job in jobs:
        for i, row in enumerate(results.get(job["id"], [])):
            if row.get("crashed"):
                continue
            score = row.get("highlight_score")
            score = float(score) if score is not None else fallback_score(row)
            fresh.append(_entry(job, i, row, gen, score))
            if job["id"].startswith(champion_prefix) and float(row.get("won", 0)) >= 1.0:
                if best_win is None or _decisiveness(row) > best_win[0]:
                    best_win = (_decisiveness(row), _entry(job, i, row, gen, score))
    n = int(cfg.get("top_n", 3))
    top, seen = [], set()
    for e in sorted(hl["top"] + fresh, key=lambda e: e["score"], reverse=True):
        key = (e["seed"], e["opponent"], e["caption"])  # same seed + same story = the same fight twice: keep one
        if key not in seen and len(top) < n:
            seen.add(key)
            top.append(e)
    hl["top"] = top
    cw = hl.get("champion_win")
    if best_win and (cw is None or cw.get("champion") != champion_id or best_win[0] > cw.get("decisiveness", -1e9)):
        hl["champion_win"] = dict(best_win[1], champion=champion_id, decisiveness=best_win[0])
    _sync_tracks(run_dir, hl)


def entries(state: dict) -> list:
    hl = state.get("highlights") or {}
    out = list(hl.get("top", []))
    cw = hl.get("champion_win")
    if cw and all(cw["id"] != e["id"] for e in out):
        out.append(dict(cw, kind="champion"))
    return out


def _sync_tracks(run_dir: str, hl: dict) -> None:
    folder = os.path.join(run_dir, "highlights", "tracks")
    os.makedirs(folder, exist_ok=True)
    keep = set()
    for e in hl["top"] + ([hl["champion_win"]] if hl.get("champion_win") else []):
        dst = os.path.join(folder, e["id"] + ".track.json")
        if not os.path.exists(dst) and e.get("source_track") and os.path.exists(e["source_track"]):
            shutil.copyfile(e["source_track"], dst)
        if os.path.exists(dst):
            e["track"] = os.path.relpath(dst, run_dir).replace("\\", "/")
            keep.add(os.path.basename(dst))
    for path in glob.glob(os.path.join(folder, "*.track.json")):
        if os.path.basename(path) not in keep:
            os.remove(path)


def prune_tracks(gen_dir: str) -> None:
    for path in glob.glob(os.path.join(gen_dir, "jobs", "*.track.json")):
        try:
            os.remove(path)
        except OSError:
            pass


def load_track(run_dir: str, entry: dict, stride: int = 2):
    """The entry's track, every `stride`-th frame (keeps report.html small)."""
    if not entry.get("track"):
        return None
    try:
        tr = load_json(os.path.join(run_dir, entry["track"]))
    except (OSError, ValueError):
        return None
    tr["frames"] = tr.get("frames", [])[::max(1, stride)]
    tr["dt"] = float(tr.get("dt", 0.2)) * max(1, stride)
    return tr


# ---- clips ----------------------------------------------------------------------------------------------------------
def _ffmpeg() -> str | None:
    return shutil.which("ffmpeg")


def encode(inputs: list, out_path: str, cfg: dict, concat_list: str | None = None) -> bool:
    """inputs -> out_path (scale to width px, H.264 crf, no audio, faststart); raises crf until <= max_mb."""
    width, crf, max_bytes = int(cfg.get("width", 960)), int(cfg.get("crf", 30)), float(cfg.get("max_mb", 25)) * 1e6
    for attempt in range(4):
        args = [_ffmpeg(), "-y", "-loglevel", "error"] + inputs + [
            "-vf", f"scale={width}:-2", "-c:v", "libx264", "-preset", "veryfast", "-crf", str(crf + 4 * attempt),
            "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-an", out_path]
        if subprocess.run(args, capture_output=True, timeout=1800).returncode != 0 or not os.path.exists(out_path):
            return False
        if os.path.getsize(out_path) <= max_bytes:
            return True
    return False


def _mock_clip(entry: dict, out_path: str, cfg: dict) -> bool:
    text = entry["caption"].replace(":", "\\:").replace("'", "")
    src = ["-f", "lavfi", "-i", "testsrc2=size=960x540:rate=30:duration=4",
           "-vf", f"drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='MOCK {text}':x=20:y=20:fontsize=26:"
                  "fontcolor=white:box=1:boxcolor=black@0.6"]
    args = [_ffmpeg(), "-y", "-loglevel", "error"] + src + ["-c:v", "libx264", "-preset", "veryfast", "-crf",
                                                            str(cfg.get("crf", 30)), "-pix_fmt", "yuv420p", out_path]
    ok = subprocess.run(args, capture_output=True, timeout=300).returncode == 0
    if not ok:  # no drawtext / font: plain test pattern
        ok = subprocess.run([_ffmpeg(), "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                             "testsrc2=size=960x540:rate=30:duration=4", "-pix_fmt", "yuv420p", out_path],
                            capture_output=True, timeout=300).returncode == 0
    return ok and os.path.exists(out_path)


def _game_clip(trainer, entry: dict, out_path: str, cfg: dict) -> bool:
    adapter = trainer.adapter
    work = os.path.join(trainer.run_dir, "highlights", "work", entry["id"])
    os.makedirs(work, exist_ok=True)
    clip_name = f"trainer_{entry['id']}"
    job = {"id": entry["id"], "weights_a": entry["weights_a"], "weights_b": entry.get("weights_b"),
           "opponent": entry["opponent"], "map": entry.get("map"), "time_limit_s": entry.get("time_limit_s"),
           "seed": entry["seed"], "rounds": 1}
    cmd = adapter.record_command(job, os.path.join(work, "rerun.arena.json"), os.path.join(work, "rerun.log"),
                                 clip_name, entry.get("duration_s") or 300)
    game = subprocess.Popen(cmd, cwd=adapter.repo_root, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    seg_dir = os.path.join(work, "segments")
    assembler = os.path.join(adapter.repo_root, "Scripts", "Tools", "clip_assemble.py")
    try:  # encodes the recorder's PNG frames block by block while the game runs; returns when the game exits
        subprocess.run([sys.executable, assembler, "watch", "--frames", adapter.frames_dir(clip_name), "--segments",
                        seg_dir, "--pid", str(game.pid), "--fps", "30"], timeout=float(cfg.get("timeout_s", 3600)))
        game.wait(timeout=600)
    except subprocess.TimeoutExpired:
        from .runner import kill_tree
        kill_tree(game)
        return False
    segments = sorted(glob.glob(os.path.join(seg_dir, "*.mp4")))
    if not segments:
        return False
    lst = os.path.join(work, "segments.txt")
    with open(lst, "w", encoding="utf-8") as f:
        f.writelines(f"file '{s.replace(os.sep, '/')}'\n" for s in segments)
    ok = encode(["-f", "concat", "-safe", "0", "-i", lst], out_path, cfg)
    if ok and not cfg.get("keep_work", False):
        shutil.rmtree(work, ignore_errors=True)
    return ok


def make_clips(trainer, state: dict, log=print, force: bool = False) -> int:
    """Renders a clip for every highlight that has none (or all with force). Returns the number made."""
    cfg = trainer.cfg.get("highlights", {})
    if not _ffmpeg():
        log("highlights: ffmpeg not found on PATH - no clips (the 2D replays in report.html still work)")
        return 0
    made = 0
    hl = state.get("highlights") or {}
    for e in hl.get("top", []) + ([hl["champion_win"]] if hl.get("champion_win") else []):
        out = os.path.join(trainer.run_dir, "highlights", e["id"] + ".mp4")
        if os.path.exists(out) and not force:
            e["clip"] = os.path.relpath(out, trainer.run_dir).replace("\\", "/")
            continue
        try:
            ok = _mock_clip(e, out, cfg) if trainer.mock else _game_clip(trainer, e, out, cfg)
        except (OSError, subprocess.SubprocessError) as ex:
            log(f"highlights: clip {e['id']} failed: {ex}")
            ok = False
        e["clip"] = os.path.relpath(out, trainer.run_dir).replace("\\", "/") if ok else None
        made += int(ok)
        log(f"highlights: clip {e['id']} {'ok' if ok else 'FAILED'} - {e['caption']}")
    return made
