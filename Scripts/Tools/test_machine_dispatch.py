"""Unit tests for Scripts/Tools/machine_dispatch.py (stdlib unittest, fake locks/processes, no real builds)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import machine_dispatch as md  # noqa: E402


class PickMachineTest(unittest.TestCase):
    def test_both_free_picks_main(self):
        main_s = md.MachineState("main", allowed=True, free=True, reason="free")
        worker_s = md.MachineState("worker", allowed=True, free=True, reason="free")
        machine, reason = md.pick_machine(main_s, worker_s)
        self.assertEqual(machine, "main")
        self.assertIn("both free", reason)

    def test_worker_busy_picks_main(self):
        main_s = md.MachineState("main", allowed=True, free=True, reason="free")
        worker_s = md.MachineState("worker", allowed=True, free=False, reason="claude (merge gate)")
        machine, reason = md.pick_machine(main_s, worker_s)
        self.assertEqual(machine, "main")
        self.assertIn("worker busy", reason)

    def test_main_busy_picks_worker(self):
        main_s = md.MachineState("main", allowed=True, free=False, reason="grok (build CodexTacticsEditor)")
        worker_s = md.MachineState("worker", allowed=True, free=True, reason="free")
        machine, reason = md.pick_machine(main_s, worker_s)
        self.assertEqual(machine, "worker")
        self.assertIn("main busy", reason)

    def test_both_busy_returns_none(self):
        main_s = md.MachineState("main", allowed=True, free=False, reason="grok (build)")
        worker_s = md.MachineState("worker", allowed=True, free=False, reason="claude (gate)")
        machine, reason = md.pick_machine(main_s, worker_s)
        self.assertIsNone(machine)
        self.assertIn("both busy", reason)


class MainGuardTest(unittest.TestCase):
    def test_flag_missing_disables_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lane = root / "lane"
            lane.mkdir()
            state = md.read_main_state(
                main_root=root,
                lane_root=lane,
                pid_alive_fn=lambda _: False,
                editor_procs_fn=lambda: [],
                worktrees_fn=lambda r: [r],
            )
            self.assertFalse(state.allowed)
            self.assertFalse(state.free)
            self.assertIn("MAIN_PC_FREE absent", state.reason)
            worker_s = md.MachineState("worker", allowed=True, free=True, reason="free")
            machine, reason = md.pick_machine(state, worker_s)
            self.assertEqual(machine, "worker")
            self.assertIn("MAIN_PC_FREE absent", reason)

    def test_editor_open_blocks_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Saved" / "Lead").mkdir(parents=True)
            (root / "Saved" / "Lead" / "MAIN_PC_FREE").write_text("1", encoding="utf-8")
            uproject = str((root / "CodexTactics.uproject").resolve())

            # Designer's interactive editor is open -> blocks main
            state = md.read_main_state(
                main_root=root,
                lane_root=root,
                pid_alive_fn=lambda _: False,
                editor_procs_fn=lambda: [{"ProcessId": 1234, "CommandLine": f'"UnrealEditor.exe" "{uproject}"'}],
                worktrees_fn=lambda r: [r],
            )
            self.assertFalse(state.allowed)
            self.assertFalse(state.free)
            self.assertIn("Editor open", state.reason)
            worker_s = md.MachineState("worker", allowed=True, free=True, reason="free")
            machine, _ = md.pick_machine(state, worker_s)
            self.assertEqual(machine, "worker")

            # A headless -game smoke process does NOT count as the designer's editor
            headless_state = md.read_main_state(
                main_root=root,
                lane_root=root,
                pid_alive_fn=lambda _: False,
                editor_procs_fn=lambda: [{"ProcessId": 5678, "CommandLine": f'"UnrealEditor.exe" "{uproject}" -game -nullrhi'}],
                worktrees_fn=lambda r: [r],
            )
            self.assertTrue(headless_state.allowed)
            self.assertTrue(headless_state.free)

    def test_stale_vs_live_local_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "Saved" / "Lead").mkdir(parents=True)
            (root / "Saved" / "Lead" / "MAIN_PC_FREE").write_text("1", encoding="utf-8")
            (root / "Saved" / "agent.lock").write_text(
                json.dumps({"agent": "grok", "purpose": "build", "pid": 9999, "manual": False, "started": "2026-11-25 18:00:00"}),
                encoding="utf-8",
            )
            stale_state = md.read_main_state(
                main_root=root,
                lane_root=root,
                pid_alive_fn=lambda _: False,
                editor_procs_fn=lambda: [],
                worktrees_fn=lambda r: [r],
            )
            self.assertTrue(stale_state.free)

            live_state = md.read_main_state(
                main_root=root,
                lane_root=root,
                pid_alive_fn=lambda pid: pid == 9999,
                editor_procs_fn=lambda: [],
                worktrees_fn=lambda r: [r],
            )
            self.assertTrue(live_state.allowed)
            self.assertFalse(live_state.free)
            self.assertEqual(live_state.lock.agent, "grok")


class FifoQueueTest(unittest.TestCase):
    def test_both_busy_waits_fifo_until_one_frees(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            queue_path = root / "Saved" / "Dispatch" / "queue.json"
            q = md.DispatchQueue(queue_path, pid_alive_fn=lambda _: True)

            # Pre-seed an earlier live waiter ahead in FIFO
            first = q.enqueue("earlier-job", root / "laneA", pid=111)
            ticks = {"n": 0}
            printed = []

            def fake_main():
                # Main stays busy until tick 3
                return md.MachineState("main", True, free=(ticks["n"] >= 3), reason="busy" if ticks["n"] < 3 else "free")

            def fake_worker():
                # Worker frees up at tick 2 (first one free!)
                return md.MachineState("worker", True, free=(ticks["n"] >= 2), reason="busy" if ticks["n"] < 2 else "free")

            def fake_sleep(_):
                ticks["n"] += 1
                # First queued job gets dispatched at tick 1 -> removed before tick 2
                if ticks["n"] == 2:
                    q.remove(str(first["id"]))

            chosen, reason = md.acquire_machine_for_job(
                job="second-job",
                lane_root=root / "laneB",
                main_root=root,
                read_main_fn=fake_main,
                read_worker_fn=fake_worker,
                queue=q,
                poll_seconds=0.01,
                sleep_fn=fake_sleep,
                print_fn=printed.append,
                max_wait_seconds=5.0,
            )
            self.assertEqual(chosen, "worker")
            self.assertIn("DISPATCH second-job -> worker", printed[0])
            self.assertEqual(q.list_entries(), [])


if __name__ == "__main__":
    unittest.main()
