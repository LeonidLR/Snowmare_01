"""grok_task.ps1 launcher: argument integrity, -s / -r session markers, billing guard, output saving (fake grok.exe)."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

TOOLS = Path(__file__).resolve().parents[1]
SCRIPT = TOOLS / "grok_task.ps1"

# Fake grok.exe: prints its own arguments as ASCII-only JSON (non-ASCII as \uXXXX) in the shape of a real grok result.
STUB_CS = r'''
using System;
using System.Text;
public static class FakeGrok {
    static string Esc(string s) {
        char bs = (char)92, dq = (char)34;
        var b = new StringBuilder(dq.ToString());
        foreach (char c in s) {
            if (c == dq || c == bs) { b.Append(bs); b.Append(c); }
            else if (c < 32 || c > 126) { b.Append(bs); b.Append("u"); b.Append(((int)c).ToString("x4")); }
            else b.Append(c);
        }
        return b.Append(dq).ToString();
    }
    public static int Main(string[] args) {
        char dq = (char)34;
        var b = new StringBuilder(dq + "args" + dq + ":[");
        for (int i = 0; i < args.Length; i++) { if (i > 0) b.Append(','); b.Append(Esc(args[i])); }
        b.Append("],");
        b.Append(dq + "text" + dq + ":" + dq + "stub-ok" + dq + ",");
        b.Append(dq + "total_cost_usd" + dq + ":0.25," + dq + "num_turns" + dq + ":1,");
        b.Append(dq + "stopReason" + dq + ":" + dq + "end_turn" + dq);
        Console.Out.Write("{" + b.ToString() + "}");
        return 0;
    }
}
'''


def ps(cmd, env=None):
    return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                          capture_output=True, text=True, encoding="utf-8", env=env)


class GrokTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.stub = root / "fake grok" / "grok.exe"  # a path with spaces on purpose
        cls.stub.parent.mkdir()
        cs = root / "stub.cs"
        cs.write_text(STUB_CS, encoding="utf-8")
        r = ps("Add-Type -Path '%s' -OutputAssembly '%s' -OutputType ConsoleApplication" % (cs, cls.stub))
        if r.returncode != 0 or not cls.stub.exists():
            raise unittest.SkipTest("cannot compile fake grok.exe: " + r.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(dir=self.tmp.name))
        self.tree = self.work / "tree dir"
        self.tree.mkdir()
        self.logs = self.work / "logs"
        self.env = {k: v for k, v in os.environ.items() if k != "XAI_API_KEY"}

    def run_task(self, prompt, session="", env=None):
        pf = self.work / "prompt file.md"
        pf.write_text(prompt, encoding="utf-8")
        cmd = "& '%s' -PromptFile '%s' -GrokPath '%s' -TreePath '%s' -LogDir '%s' -Label t" % (
            SCRIPT, pf, self.stub, self.tree, self.logs)
        if session:
            cmd += " -Session '%s'" % session
        return ps(cmd, env or self.env)

    def last_args(self):
        out = sorted(self.logs.glob("*-t.json"))[-1]
        return json.loads(out.read_text(encoding="utf-8"))["args"]

    def test_prompt_arrives_as_one_intact_argument(self):
        bs = chr(92)  # backslashes before a quote / at the end are the classic CommandLineToArgvW traps
        prompt = chr(10).join([
            'He said "hi" and ' + bs + '"escaped' + bs + '" ' + bs + bs + '"x',
            'line two' + bs,
            'Привет мир',
            'C:' + bs + 'Program Files' + bs + 'My App' + bs + ' end' + bs,
        ])
        r = self.run_task(prompt)
        self.assertEqual(r.returncode, 0, r.stderr)
        args = self.last_args()
        self.assertEqual(args[0], "-p")
        self.assertEqual(args[1].replace("\r\n", "\n"), prompt)
        self.assertEqual(args.count("-p"), 1)
        self.assertEqual(args[args.index("--cwd") + 1], str(self.tree))

    def test_session_first_s_then_r(self):
        self.assertEqual(self.run_task("one", "grok-selftest-x").returncode, 0)
        a1 = self.last_args()
        self.assertIn("-s", a1)
        self.assertNotIn("-r", a1)
        guid = a1[a1.index("-s") + 1]
        time.sleep(1.1)  # the output file name carries the second
        self.assertEqual(self.run_task("two", "grok-selftest-x").returncode, 0)
        a2 = self.last_args()
        self.assertIn("-r", a2)
        self.assertNotIn("-s", a2)
        self.assertEqual(a2[a2.index("-r") + 1], guid)

    def test_api_key_guard_throws(self):
        r = self.run_task("x", env=dict(self.env, XAI_API_KEY="test-value"))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("XAI_API_KEY", r.stderr + r.stdout)
        self.assertFalse(self.logs.exists() and list(self.logs.glob("*-t.json")))

    def test_output_saved_and_summary_printed(self):
        r = self.run_task("x")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(list(self.logs.glob("*-t.json")))
        self.assertRegex(r.stdout, r"done: .*-t\.json\s+cost 0\.25 USD\s+turns 1\s+stop end_turn")
        self.assertIn("stub-ok", r.stdout)


if __name__ == "__main__":
    unittest.main()
