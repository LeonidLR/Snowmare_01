"""THE ONE PLACE that knows the arena's command line and result format (fit it here when CodexTactics.PmcArena lands).

Everything is driven by trainer_config.json "arena" so most changes are config-only:
  - arg_template: the console-command arguments ({map} {rounds} {seed} {weights_a} {opponent} {weights_b}
    {time_limit_s} {out}); paths are passed PROJECT-RELATIVE with forward slashes (no spaces: -ExecCmds splits on
    spaces, commas separate commands), the arena resolves them against FPaths::ProjectDir().
  - result: schema id, key names and the field map from the arena's per-side keys to the canonical names below.

Canonical round (candidate's point of view), consumed by fitness.py:
  won (1 / 0.5 draw / 0), duration_s, time_limit_s, survivors, squad_size, hp_lost, hp_max, kills,
  flanks_attempted, flanks_succeeded, suppression_s, ambushes, retreats, retreats_survived, heals,
  friendly_fire, stuck_events, crashed (False here; True rows come from the runner on a crash / timeout).
"""
from __future__ import annotations

import glob
import os
import sys

from . import track_stats
from .genome import load_json

CANONICAL_SIDE_FIELDS = ("survivors", "squad_size", "hp_lost", "hp_max", "kills", "flanks_attempted",
                         "flanks_succeeded", "suppression_s", "ambushes", "retreats", "retreats_survived",
                         "heals", "friendly_fire", "stuck_events")


class ArenaResultError(ValueError):
    pass


def _rel(path: str, root: str) -> str:
    try:
        rel = os.path.relpath(path, root)
    except ValueError:  # another drive
        rel = path
    return rel.replace("\\", "/")


def _dig(obj, dotted: str):
    for part in dotted.split("."):
        if isinstance(obj, dict) and part in obj:
            obj = obj[part]
        else:
            return None
    return obj


class ArenaAdapter:
    def __init__(self, arena_cfg: dict, repo_root: str, mock_cfg: dict | None = None, close_range_cfg: dict | None = None):
        # fitness.close_range (trainer_config.json): when set, rows get close_shot_ratio / close_hits_taken from the track
        self.close_range_cfg = close_range_cfg
        self.cfg = arena_cfg
        self.repo_root = os.path.abspath(repo_root)
        self.mock_cfg = mock_cfg  # not None = mock mode (synthetic arena, mock_arena.py)
        self.result_cfg = arena_cfg.get("result", {})

    # ---- command line -------------------------------------------------------------------------------------------
    def console_command(self, job: dict, out_path: str) -> str:
        names = self.cfg.get("opponent_names", {"bot": "Bot", "champion": "Pmc"})
        values = {
            "map": job.get("map") or self.cfg.get("map", ""),
            "rounds": job["rounds"],
            "seed": job["seed"],
            "weights_a": _rel(job["weights_a"], self.repo_root),
            "opponent": names.get(job["opponent"], job["opponent"]),
            "weights_b": _rel(job["weights_b"], self.repo_root) if job.get("weights_b") else "None",
            "time_limit_s": job.get("time_limit_s") or self.cfg.get("time_limit_s", 300),
            "out": _rel(out_path, self.repo_root),
        }
        parts = [self.cfg.get("console_command", "CodexTactics.PmcArena")]
        parts += [str(t).format(**values) for t in self.cfg.get("arg_template", [])]
        return " ".join(parts)

    def build_command(self, job: dict, out_path: str, log_path: str) -> list:
        """argv of one arena process. Mock mode runs mock_arena.py with the same job instead of the game."""
        if self.mock_cfg is not None:
            cmd = [sys.executable, os.path.join(os.path.dirname(__file__), "mock_arena.py"),
                   "--weights-a", job["weights_a"], "--opponent", job["opponent"], "--rounds", str(job["rounds"]),
                   "--seed", str(job["seed"]), "--out", out_path, "--reference", self.mock_cfg["reference_weights"],
                   "--crash-rate", str(self.mock_cfg.get("crash_rate", 0.0)),
                   "--bot-strength", str(self.mock_cfg.get("bot_strength", -0.03))]
            if job.get("weights_b"):
                cmd += ["--weights-b", job["weights_b"]]
            if self.mock_cfg.get("tracks", True):
                cmd.append("--tracks")
            return cmd
        return self._game_argv(job, list(self.cfg.get("engine_args", [])), self.console_command(job, out_path),
                               log_path)

    def _game_argv(self, job: dict, engine_args: list, exec_cmds: str, log_path: str) -> list:
        engine = os.path.join(self.cfg.get("engine_root", "C:/Program Files/Epic Games/UE_5.8"),
                              "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")
        uproject = os.path.join(self.repo_root, self.cfg.get("uproject", "CodexTactics.uproject"))
        map_name = job.get("map") or self.cfg.get("map", "")
        return ([engine, uproject] + ([map_name] if map_name else []) + engine_args
                + [f"-ExecCmds={exec_cmds}", f"-abslog={log_path}"])

    def record_command(self, job: dict, out_path: str, log_path: str, clip_name: str, seconds: float) -> list:
        """Deterministic re-run of ONE round (job["seed"] = that round's seed, rounds 1) rendered offscreen, with the
        repo's frame recorder chained in -ExecCmds (frames -> Scripts/Tools/clip_assemble.py -> mp4)."""
        rec = self.cfg.get("record", {})
        arena = self.console_command(dict(job, rounds=1), out_path)
        extra = " ".join(str(t).format(seed=job["seed"]) for t in rec.get("arg_template_extra", ["Record=1"]))
        clip = str(rec.get("clip_command", "CodexTactics.ClipRecorder {clip} seconds={seconds}")).format(
            clip=clip_name, seconds=int(seconds) + 3)
        return self._game_argv(job, list(rec.get("engine_args", [])), f"{arena} {extra},{clip}", log_path)

    def frames_dir(self, clip_name: str) -> str:
        rec = self.cfg.get("record", {})
        return os.path.join(self.repo_root, str(rec.get("frames_dir", "Saved/Screenshots/Clips/{clip}")).format(clip=clip_name))

    def find_output(self, out_path: str, started_at: float):
        """The result JSON: the requested Out path, else the newest <output_dir>/*.json written after start."""
        if os.path.exists(out_path):
            return out_path
        pattern = os.path.join(self.repo_root, self.cfg.get("output_dir", "Saved/Arena"), "*.json")
        fresh = [p for p in glob.glob(pattern) if os.path.getmtime(p) >= started_at - 1.0]
        return max(fresh, key=os.path.getmtime) if fresh else None

    # ---- result parsing -----------------------------------------------------------------------------------------
    def parse_result(self, path: str, expected_rounds: int | None = None) -> list:
        return self.parse_doc(load_json(path), expected_rounds, os.path.dirname(os.path.abspath(path)))

    def parse_doc(self, doc: dict, expected_rounds: int | None = None, result_dir: str | None = None) -> list:
        rc = self.result_cfg
        schema = rc.get("schema", "codex.pmc_arena/1")
        if schema and doc.get("schema") != schema and rc.get("strict_schema", True):
            raise ArenaResultError(f"unexpected schema {doc.get('schema')!r} (want {schema!r})")
        rounds = _dig(doc, rc.get("rounds_key", "rounds"))
        if not isinstance(rounds, list) or not rounds:
            raise ArenaResultError("no rounds in the arena result")
        mine, theirs = str(rc.get("candidate_side", "A")), str(rc.get("opponent_side", "B"))
        draws = {str(v).lower() for v in rc.get("draw_values", ["draw", "none", ""])}
        fields = rc.get("fields", {})
        default_limit = _dig(doc, rc.get("time_limit_key", "time_limit_s"))
        out = []
        for rnd in rounds:
            winner = _dig(rnd, rc.get("winner_key", "winner"))
            w = "" if winner is None else str(winner)
            if w == mine:
                won = 1.0
            elif w == theirs:
                won = 0.0
            elif w.lower() in draws:
                won = 0.5
            else:
                raise ArenaResultError(f"unknown winner {winner!r}")
            side = self._side(rnd, mine)
            row = {"won": won, "crashed": False,
                   "duration_s": float(_dig(rnd, rc.get("duration_key", "duration_s")) or 0.0),
                   "time_limit_s": float(_dig(rnd, rc.get("time_limit_key", "time_limit_s")) or default_limit or 0.0)}
            for key in CANONICAL_SIDE_FIELDS:
                value = _dig(side, fields.get(key, key))
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    row[key] = float(value)
            # highlight extras (optional in the arena result): watching material, not fitness
            opp = self._side(rnd, theirs)
            for key, src in (("opp_survivors", _dig(opp, fields.get("survivors", "survivors"))),
                             ("opp_kills", _dig(opp, fields.get("kills", "kills"))),
                             ("opp_hp_lost", _dig(opp, fields.get("hp_lost", "hp_lost"))),
                             ("highlight_score", _dig(rnd, rc.get("highlight_key", "highlight_score"))),
                             ("round_seed", _dig(rnd, rc.get("round_seed_key", "seed"))),
                             ("round_index", _dig(rnd, rc.get("round_index_key", "index")))):
                if isinstance(src, (int, float)) and not isinstance(src, bool):
                    row[key] = float(src) if key != "round_seed" else int(src)
            end_reason = _dig(rnd, rc.get("end_reason_key", "end_reason"))
            if end_reason:
                row["end_reason"] = str(end_reason)
            track = _dig(rnd, rc.get("track_key", "track"))
            if track and result_dir:
                row["track"] = track if os.path.isabs(track) else os.path.normpath(os.path.join(result_dir, track))
                if self.close_range_cfg:  # parse now: the trainer prunes tracks after the generation
                    st = track_stats.load_side_stats(row["track"], mine,
                                                     float(self.close_range_cfg.get("close_range_m", 10.0)),
                                                     bool(self.close_range_cfg.get("shotgun_exempt", True)))
                    if st and st["shots"]:
                        row["close_shot_ratio"] = float(st["close_shot_ratio"])
                    if st:
                        row["close_hits_taken"] = float(st["close_hits_taken"])
            out.append(row)
        if expected_rounds and len(out) < expected_rounds and rc.get("pad_missing_rounds_as_crash", True):
            out += [{"crashed": True, "won": 0.0}] * (expected_rounds - len(out))
        return out

    def _side(self, rnd: dict, side_id: str) -> dict:
        sides = _dig(rnd, self.result_cfg.get("sides_key", "sides"))
        if isinstance(sides, dict):
            return sides.get(side_id) or {}
        if isinstance(sides, list):
            for s in sides:
                if isinstance(s, dict) and str(s.get("side", s.get("id"))) == side_id:
                    return s
        return {}
