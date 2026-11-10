"""The evolution loop with on-disk state under Saved/Trainer/<run_id>/ (resumable at any point).

Layout:
  run.json                 config snapshot, seed, baseline source (written once; a resume reuses it)
  baseline_weights.json    frozen copy of the baseline weights;  bounds.json  frozen copy of the bounds sidecar
  state.json               last finished generation, the population to evaluate next, champion, history
  status.json              heartbeat for status.ps1 (state, generation, jobs done, best, ETA)
  gen_NNNN/candidates/*.json   full weights files;  gen_NNNN/opponent_champion.json  the self-play opponent
  gen_NNNN/jobs/*.arena.json|*.rounds.json|*.log   arena results (cached: a restart re-runs only missing jobs)
  gen_NNNN/generation.json     per-candidate fitness + stats, fresh-seed re-evaluation, promotion decision
  champion_weights.json + champion_meta.json + champions/<id>.json   current champion and the history
  leaderboard.json / leaderboard.csv / report.html / trainer.log
Every random choice is seeded from (run seed, generation), so a resumed run equals an uninterrupted one.
"""
from __future__ import annotations

import hashlib
import os
import shutil
import time

from . import evolution, highlights, report
from .adapter import ArenaAdapter
from .fitness import aggregate_by_opponent
from .genome import (apply_genes, flatten_numeric, load_json, load_or_create_bounds, resolve_specs, write_json)
from .runner import Guard, JobRunner, now_iso


def seed_for(run_seed: int, generation: int, stream: str, index: int) -> int:
    h = hashlib.sha1(f"{run_seed}:{generation}:{stream}:{index}".encode("utf-8")).hexdigest()
    return int(h[:8], 16) % 2_000_000_000


class Trainer:
    def __init__(self, cfg: dict, run_id: str, repo_root: str, mock: bool = False, log=print,
                 deadline: float | None = None, max_generations: int | None = None, parallel: int | None = None,
                 yield_to_editor: bool = False, mock_in_process: bool | None = None, poll_s: float = 30.0):
        self.repo_root = os.path.abspath(repo_root)
        self.run_id = run_id
        self.run_dir = os.path.normpath(os.path.join(self.repo_root, cfg.get("runs_dir", "Saved/Trainer"), run_id))
        self.log_fn = log
        self.deadline = deadline
        self.yield_to_editor = yield_to_editor
        self.poll_s = poll_s
        self._setup(cfg, mock)
        c = self.cfg
        self.max_generations = int(max_generations if max_generations is not None
                                   else c["evolution"].get("max_generations", 1000))
        self.parallel = int(parallel or c["arena"].get("parallel", 2))
        if mock_in_process is None:
            mock_in_process = bool(c.get("mock", {}).get("in_process", False))
        mock_cfg = dict(c.get("mock", {}), reference_weights=self.baseline_path) if self.mock else None
        self.adapter = ArenaAdapter(c["arena"], self.repo_root, mock_cfg, c.get("fitness", {}).get("close_range"))
        self.runner = JobRunner(self.adapter, c["arena"], self.parallel, self.mock and mock_in_process, mock_cfg,
                                self.log)
        self.status = {}

    # ---- setup / persistence ----------------------------------------------------------------------------------------
    def _setup(self, cfg: dict, mock: bool) -> None:
        run_json = os.path.join(self.run_dir, "run.json")
        self.baseline_path = os.path.join(self.run_dir, "baseline_weights.json")
        if os.path.exists(run_json):
            meta = load_json(run_json)
            self.cfg, self.mock, self.run_seed = meta["config"], bool(meta["mock"]), int(meta["seed"])
            self.baseline_source = meta["baseline_source"]
        else:
            self.cfg, self.mock, self.run_seed = cfg, mock, int(cfg.get("seed", 1))
            source = os.path.join(self.repo_root, cfg["baseline_weights"])
            if mock and not os.path.exists(source):
                source = os.path.join(self.repo_root, cfg["mock"]["baseline_weights"])
            if not os.path.exists(source):
                raise FileNotFoundError(f"baseline weights not found: {source}")
            bounds_src = os.path.join(self.repo_root, cfg["bounds"]) if cfg.get("bounds") else None
            bounds, created = load_or_create_bounds(source, bounds_src,
                                                    float(cfg.get("evolution", {}).get("default_bounds_rel", 0.5)))
            os.makedirs(self.run_dir, exist_ok=True)
            shutil.copyfile(source, self.baseline_path)
            write_json(os.path.join(self.run_dir, "bounds.json"), bounds)
            self.baseline_source = os.path.relpath(source, self.repo_root).replace("\\", "/")
            write_json(run_json, {"run_id": self.run_id, "created": now_iso(), "mock": mock, "seed": self.run_seed,
                                  "baseline_source": self.baseline_source, "bounds_created": created, "config": cfg})
            if created:
                self.log(f"bounds sidecar created next to {self.baseline_source} (defaults +-50 %)")
        self.baseline_doc = load_json(self.baseline_path)
        self.baseline_genes = flatten_numeric(self.baseline_doc)
        self.specs = resolve_specs(self.baseline_genes, load_json(os.path.join(self.run_dir, "bounds.json")))

    def log(self, msg: str) -> None:
        line = f"[{now_iso()}] {msg}"
        self.log_fn(line)
        try:
            with open(os.path.join(self.run_dir, "trainer.log"), "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError:
            pass

    def load_state(self) -> dict:
        path = os.path.join(self.run_dir, "state.json")
        if os.path.exists(path):
            return load_json(path)
        return {"generation_done": -1, "population": None, "entries": [], "history": [], "baseline_stats": None,
                "champion": {"id": "baseline", "genes": dict(self.baseline_genes), "promoted_generation": -1,
                             "stats": None}, "champions": []}

    def save_state(self, state: dict) -> None:
        write_json(os.path.join(self.run_dir, "state.json"), state)

    def set_status(self, state_name: str, note: str = "", **extra) -> None:
        self.status.update(extra)
        self.status.update({"run_id": self.run_id, "state": state_name, "note": note, "updated": now_iso(),
                            "pid": os.getpid(), "mock": self.mock, "max_generations": self.max_generations,
                            "deadline": (time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(self.deadline))
                                         if self.deadline else None)})
        # The status files are informational: a locked file must never stop the training.
        try:
            write_json(os.path.join(self.run_dir, "status.json"), self.status)
            write_json(os.path.join(os.path.dirname(self.run_dir), "latest.json"),
                       {"run_id": self.run_id, "run_dir": self.run_dir, "updated": self.status["updated"]})
        except OSError as error:
            print(f"[{now_iso()}] status write skipped: {error}", flush=True)

    # ---- main loop --------------------------------------------------------------------------------------------------
    def run(self) -> str:
        state = self.load_state()
        self.save_state(state)
        guard = Guard(self.run_dir, self.repo_root, self.deadline, check_lock=not self.mock,
                      hold_lock=(not self.mock) and bool(self.cfg["arena"].get("hold_build_lock", True)),
                      yield_to_editor=self.yield_to_editor, on_status=lambda s, n="": self.set_status(s, n),
                      poll_s=self.poll_s)
        self.log(f"run {self.run_id} ({'mock' if self.mock else 'arena'}): resume after generation "
                 f"{state['generation_done']}, max {self.max_generations}, parallel {self.parallel}")
        outcome = "finished"
        while state["generation_done"] + 1 < self.max_generations:
            if guard.stop_requested():
                outcome = "stopped"
                break
            if not self.run_generation(state, guard):
                outcome = "stopped"
                break
        if self.cfg.get("highlights", {}).get("auto_clips", True) and state.get("highlights"):
            self.make_highlights(state)
        self.set_status(outcome, guard.stopped_reason or "", generation_done=state["generation_done"])
        report.write_all(self, state)
        self.log(f"run {self.run_id} {outcome} ({guard.stopped_reason or 'max generations'}) after generation "
                 f"{state['generation_done']}")
        return outcome

    def run_generation(self, state: dict, guard: Guard) -> bool:
        gen = state["generation_done"] + 1
        evo = self.cfg["evolution"]
        if state["population"] is None:
            state["population"] = evolution.initial_population(
                self.baseline_genes, self.specs, int(evo.get("population", 12)),
                evolution.generation_rng(self.run_seed, gen, "init"), evo)
            self.save_state(state)
        population = state["population"]
        gen_dir = os.path.join(self.run_dir, f"gen_{gen:04d}")
        started = time.time()
        if not os.path.exists(os.path.join(gen_dir, "started.json")):
            write_json(os.path.join(gen_dir, "started.json"), {"started": now_iso(), "t": started})
        else:
            started = float(load_json(os.path.join(gen_dir, "started.json"))["t"])
        opponent_path = os.path.join(gen_dir, "opponent_champion.json")
        if not os.path.exists(opponent_path):
            write_json(opponent_path, apply_genes(self.baseline_doc, state["champion"]["genes"]))
        cands = []
        for i, genes in enumerate(population):
            cid = f"g{gen:04d}_c{i:02d}"
            path = os.path.join(gen_dir, "candidates", cid + ".json")
            if not os.path.exists(path):
                write_json(path, apply_genes(self.baseline_doc, genes))
            cands.append({"id": cid, "genes": genes, "path": path})

        # 1. evaluation vs the champion (self-play) and the bot-driven player squad, common seeds per generation
        jobs = []
        for c in cands:
            jobs += self.make_jobs(c["id"], c["path"], gen, "eval", gen_dir, opponent_path)
        self.set_status("running", generation=gen, phase="eval", jobs_total=len(jobs), jobs_done=0)
        results = self.runner.run_jobs(jobs, guard, self._job_done)
        if results is None:
            return False
        for c in cands:
            c["by_opp"] = self.rounds_by_opponent(c["id"], jobs, results)
            c["agg"] = aggregate_by_opponent(c["by_opp"], self.cfg["fitness"])
            c["fitness"] = c["agg"]["all"]["fitness"]

        # 2. anti-overfit: top-k + the champion re-evaluated on FRESH seeds; promote only on a fresh-seed win
        ranked = sorted(cands, key=lambda c: c["fitness"], reverse=True)
        champ = state["champion"]
        contenders = [c for c in ranked if c["genes"] != champ["genes"]][: int(evo.get("fresh_top_k", 3))]
        fresh_jobs = self.make_jobs("champion_" + f"g{gen:04d}", opponent_path, gen, "fresh", gen_dir, opponent_path)
        for c in contenders:
            fresh_jobs += self.make_jobs(c["id"], c["path"], gen, "fresh", gen_dir, opponent_path)
        self.set_status("running", generation=gen, phase="fresh", jobs_total=len(fresh_jobs), jobs_done=0)
        fresh = self.runner.run_jobs(fresh_jobs, guard, self._job_done)
        if fresh is None:
            return False
        champ_fresh = aggregate_by_opponent(self.rounds_by_opponent("champion_" + f"g{gen:04d}", fresh_jobs, fresh),
                                            self.cfg["fitness"])
        for c in contenders:
            c["fresh"] = aggregate_by_opponent(self.rounds_by_opponent(c["id"], fresh_jobs, fresh), self.cfg["fitness"])
        if state["baseline_stats"] is None and champ["id"] == "baseline":
            state["baseline_stats"] = champ_fresh
        best = max(contenders, key=lambda c: c["fresh"]["all"]["fitness"], default=None)
        margin = float(evo.get("promotion_margin", 0.0))
        promoted = bool(best and best["fresh"]["all"]["fitness"] > champ_fresh["all"]["fitness"] + margin)
        champion_before = champ["id"]
        if promoted:
            state["champion"] = {"id": best["id"], "genes": best["genes"], "promoted_generation": gen,
                                 "stats": best["fresh"], "beat": {"id": champ["id"], "stats": champ_fresh}}
            state["champions"].append({"id": best["id"], "generation": gen, "fresh_fitness":
                                       best["fresh"]["all"]["fitness"], "previous": champ["id"],
                                       "previous_fresh_fitness": champ_fresh["all"]["fitness"]})
            self.log(f"gen {gen}: NEW CHAMPION {best['id']} fresh {best['fresh']['all']['fitness']:.3f} > "
                     f"{champ['id']} {champ_fresh['all']['fitness']:.3f}")
        else:
            state["champion"]["stats"] = champ_fresh
        hl_cfg = self.cfg.get("highlights", {})
        if hl_cfg.get("enabled", True):
            highlights.update(self.run_dir, hl_cfg, state, gen, jobs + fresh_jobs, {**results, **fresh},
                              state["champion"]["id"],
                              (best["id"] + "_fresh_") if promoted else f"champion_g{gen:04d}_")
            if not hl_cfg.get("keep_all_tracks", False):
                highlights.prune_tracks(gen_dir)

        # 3. bookkeeping, next population, outputs
        fits = [c["fitness"] for c in cands]
        finished = time.time()
        gen_doc = {"generation": gen, "started": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(started)),
                   "finished": now_iso(), "duration_s": round(finished - started, 1),
                   "champion_before": champion_before, "champion_after": state["champion"]["id"],
                   "promoted": promoted, "champion_fresh": champ_fresh,
                   "candidates": [{"id": c["id"], "fitness": c["fitness"], "stats": c["agg"],
                                   "fresh": c.get("fresh")} for c in ranked]}
        write_json(os.path.join(gen_dir, "generation.json"), gen_doc)
        for c in ranked:
            a, bot = c["agg"]["all"], c["agg"].get("bot", {})
            state["entries"].append({"id": c["id"], "generation": gen, "fitness": c["fitness"],
                                     "fresh_fitness": c.get("fresh", {}).get("all", {}).get("fitness"),
                                     "win_rate": a.get("win_rate"), "win_rate_vs_bot": bot.get("win_rate"),
                                     "survivors_frac": a.get("survivors_frac"), "hp_lost": a.get("hp_lost"),
                                     "duration_s": a.get("duration_s"), "crashes": a.get("crashes"),
                                     "rounds": a.get("rounds")})
        state["history"].append({"generation": gen, "best": max(fits), "mean": round(sum(fits) / len(fits), 4),
                                 "best_id": ranked[0]["id"], "champion": state["champion"]["id"],
                                 "champion_fresh": champ_fresh["all"]["fitness"], "promoted": promoted,
                                 "duration_s": gen_doc["duration_s"], "finished": gen_doc["finished"]})
        state["population"] = evolution.next_generation([(c["genes"], c["fitness"]) for c in cands], self.specs,
                                                        evolution.generation_rng(self.run_seed, gen, "evo"), evo)
        state["generation_done"] = gen
        self.save_state(state)
        self.write_champion(state)
        report.write_all(self, state)
        self.log(f"gen {gen}: best {max(fits):.3f} ({ranked[0]['id']}) mean {state['history'][-1]['mean']:.3f} "
                 f"champion {state['champion']['id']} ({gen_doc['duration_s']} s)")
        self.set_status("running", generation=gen + 1, generation_done=gen, best_fitness=max(fits),
                        champion=state["champion"]["id"], eta=self.eta(state))
        return True

    def check(self, rounds: int, jobs: int = 1, clips: bool = True) -> dict:
        """Check mode (fix/pmc-arena-stalemate 2026-11-10): the baseline weights against themselves, `jobs` x `rounds` arena
        rounds - no evolution, no champion change. Highlights (+ clips) and the fight-flow stats (stalemate_stats) go to
        <run>/check/ and the report; returns the stats summary."""
        from . import stalemate_stats
        state = self.load_state()
        guard = Guard(self.run_dir, self.repo_root, self.deadline, check_lock=not self.mock,
                      hold_lock=(not self.mock) and bool(self.cfg["arena"].get("hold_build_lock", True)),
                      yield_to_editor=self.yield_to_editor, on_status=lambda s, n="": self.set_status(s, n),
                      poll_s=self.poll_s)
        check_dir = os.path.join(self.run_dir, "check")
        job_list = [{"id": f"check_baseline_champion_{i}", "dir": os.path.join(check_dir, "jobs"),
                     "weights_a": self.baseline_path, "opponent": "champion", "weights_b": self.baseline_path,
                     "rounds": int(rounds), "seed": seed_for(self.run_seed, 0, "check", i),
                     "map": self.cfg["arena"].get("map"), "time_limit_s": self.cfg["arena"].get("time_limit_s", 300)}
                    for i in range(int(jobs))]
        self.log(f"check {self.run_id}: {jobs} job(s) x {rounds} round(s), baseline vs baseline, no evolution")
        self.set_status("running", phase="check", jobs_total=len(job_list), jobs_done=0)
        results = self.runner.run_jobs(job_list, guard, self._job_done)
        if results is None:
            self.set_status("stopped", guard.stopped_reason or "")
            return {}
        rows = []
        for job in job_list:
            result = os.path.join(job["dir"], job["id"] + ".arena.json")
            if os.path.exists(result):
                rows += stalemate_stats.stats_for_path(result)
        summary = stalemate_stats.summary(rows)
        write_json(os.path.join(check_dir, "flow_stats.json"), {"rows": rows, "summary": summary})
        self.log(f"check flow: {summary}")
        hl_cfg = dict(self.cfg.get("highlights", {}), top_n=int(self.cfg.get("highlights", {}).get("top_n", 3)))
        highlights.update(self.run_dir, hl_cfg, state, 0, job_list, results, "baseline", "check_baseline_")
        self.save_state(state)
        if clips and state.get("highlights"):
            self.make_highlights(state)
        self.set_status("finished", "check")
        report.write_all(self, state)
        return summary

    def make_highlights(self, state: dict | None = None, force: bool = False) -> int:
        """Clips for the session's highlights (after the night's last generation, or make_highlights.ps1)."""
        state = state or self.load_state()
        self.set_status("clips", "recording highlight clips")
        made = highlights.make_clips(self, state, self.log, force)
        self.save_state(state)
        report.write_all(self, state)
        return made

    def make_jobs(self, cid: str, weights: str, gen: int, stream: str, gen_dir: str, opponent_path: str) -> list:
        ev = self.cfg["evaluation"]
        jobs, index = [], 0
        for match in ev.get("matches", [{"opponent": "champion", "jobs": 1}, {"opponent": "bot", "jobs": 1}]):
            for _ in range(int(match.get("jobs", 1))):
                opp = match["opponent"]
                jobs.append({"id": f"{cid}_{stream}_{opp}_{index}", "dir": os.path.join(gen_dir, "jobs"),
                             "weights_a": weights, "opponent": opp,
                             "weights_b": opponent_path if opp == "champion" else None,
                             "rounds": int(ev.get("rounds_per_job", 4)),
                             "seed": seed_for(self.run_seed, gen, stream, index),
                             "map": match.get("map") or self.cfg["arena"].get("map"),
                             "time_limit_s": self.cfg["arena"].get("time_limit_s", 300)})
                index += 1
        return jobs

    @staticmethod
    def rounds_by_opponent(cid: str, jobs: list, results: dict) -> dict:
        out = {}
        for job in jobs:
            if job["id"].startswith(cid + "_"):
                out.setdefault(job["opponent"], []).extend(results.get(job["id"], []))
        return out

    def _job_done(self, job: dict) -> None:
        self.status["jobs_done"] = int(self.status.get("jobs_done", 0)) + 1
        self.set_status("running")

    def eta(self, state: dict):
        done = [h["duration_s"] for h in state["history"] if h.get("duration_s")]
        if not done:
            return None
        avg = sum(done[-5:]) / len(done[-5:])
        remaining = self.max_generations - (state["generation_done"] + 1)
        end = time.time() + avg * remaining
        if self.deadline:
            end = min(end, self.deadline)
        return {"avg_generation_s": round(avg, 1), "generations_left": remaining,
                "finish_by": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(end))}

    def write_champion(self, state: dict) -> None:
        champ = state["champion"]
        doc = apply_genes(self.baseline_doc, champ["genes"])
        write_json(os.path.join(self.run_dir, "champion_weights.json"), doc)
        write_json(os.path.join(self.run_dir, "champions", champ["id"] + ".json"), doc)
        write_json(os.path.join(self.run_dir, "champion_meta.json"),
                   {"id": champ["id"], "promoted_generation": champ["promoted_generation"],
                    "baseline_source": self.baseline_source, "stats": champ.get("stats"),
                    "history": state["champions"], "note": "Apply with Scripts/Trainer/apply_champion.ps1 -RunId "
                    + self.run_id + " (the trainer never overwrites the game file itself)."})
