#!/usr/bin/env python3
"""Jev digest: turns long game / test logs into a few lines, so an agent reads a verdict instead of the raw log.

Code extracts the facts (results, check lines, errors, counts); TypeSafe Jev is asked only when a judgment helps (why a
smoke / test failed, how a bot batch played) - one request per call. Without a key the facts are still printed.

Usage:
  python Scripts/Tools/jev_digest.py smoke <Smoke-X.log | X> [...]    one line per smoke (+ Jev's failure class on FAIL)
  python Scripts/Tools/jev_digest.py tests [Saved/Logs/AutomationTests.log]
  python Scripts/Tools/jev_digest.py bot [--profile VETERAN] [--runs 8]   the last bot batch in 4 lines + Jev's read
  python Scripts/Tools/jev_digest.py log <file> --ask "yes/no question" [--grep REGEX]
Exit code: 1 when a smoke / test failed (scripts can chain on it).
"""

import argparse
import os
import re
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(__file__))
import jev_client  # noqa: E402

ROOT = jev_client.ROOT
LOGS = os.path.join(ROOT, "Saved", "Logs")
ERROR_RE = re.compile(r"(Error:|Fatal error|Assertion failed|Ensure condition failed|Unhandled Exception|EXCEPTION_)", re.I)
# Engine noise in every log (the experimental ToolsetRegistry plugin's Python start-up): never evidence.
NOISE_RE = re.compile(r"LogPython|ToolsetRegistry|init_unreal")
FAILURE_CLASSES = {
    "crash": "The game crashed (fatal error, assertion, access violation)",
    "timeout": "The check never finished: it timed out waiting for something to happen",
    "setup": "The test could not set up its scene (no squad, no spawn, missing asset or map object)",
    "navigation": "Units did not get where they should (no path, stuck, blocked by geometry)",
    "behaviour": "The AI or gameplay rule did something other than the check expected",
    "numbers": "A value was off: damage, timing, distance or count outside the expected range",
}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def classify(evidence):
    answers, error = jev_client.ask({"evidence": evidence[-25:]}, {"cause": {
        "type": "choice", "instructions": "The lines in `evidence` come from a failed automated check of a UE5 game. "
                                          "What kind of failure is it?",
        "criteria": FAILURE_CLASSES}})
    cause, confidence = jev_client.choice(answers, "cause")
    return (f"{cause} (conf {confidence:.2f})" if cause else f"unclassified ({error})")


def cmd_smoke(names):
    failed = 0
    for name in names:
        path = name if os.path.exists(name) else os.path.join(LOGS, name if name.endswith(".log") else f"Smoke-{name}.log")
        if not os.path.exists(path):
            print(f"{name}: no log")
            failed += 1
            continue
        lines = read(path)
        result = next((l.split("Smoke RESULT:")[1].strip() for l in reversed(lines) if "Smoke RESULT:" in l), "NO RESULT")
        checks = [l.split("Smoke ", 1)[1] for l in lines if "LogCodexTactics" in l and "Smoke " in l and "RESULT" not in l]
        fails = [c for c in checks if c.startswith("FAIL")]
        errors = [l.strip()[-200:] for l in lines if ERROR_RE.search(l) and not NOISE_RE.search(l)][-5:]
        label = re.sub(r"^Smoke-|\.log$", "", os.path.basename(path))
        if result == "PASS":
            print(f"{label}: PASS ({len(checks)} checks)")
            continue
        failed += 1
        print(f"{label}: {result} - {len(fails)} failed checks; Jev: {classify(fails + errors + checks[-5:])}")
        for line in (fails or checks[-3:])[:3]:
            print(f"    {line[:180]}")
        for line in errors[:2]:
            print(f"    ! {line}")
    return 1 if failed else 0


def cmd_tests(path):
    lines = read(path)
    results = re.findall(r"Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}", "\n".join(lines))
    failed = [p for status, p in results if status != "Success"]
    print(f"tests: {len(results)} run, {len(failed)} failed")
    if not failed:
        return 0 if results else 1
    errors = [l.strip()[-220:] for l in lines if (ERROR_RE.search(l) or "Expected" in l) and not NOISE_RE.search(l)][-12:]
    print(f"Jev: {classify(errors + failed)}")
    for name in failed[:5]:
        print(f"    FAIL {name}")
    for line in errors[-4:]:
        print(f"    ! {line}")
    return 1


def cmd_bot(profile, runs):
    import importlib.util
    spec = importlib.util.spec_from_file_location("coach", os.path.join(os.path.dirname(__file__), "jev_ai_coach.py"))
    coach = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(coach)
    args = argparse.Namespace(profile=profile, runs=runs)
    facts = coach.collect_facts(args)
    s = facts["summary"]
    print(f"bot {profile}: {s['victories']}/{s['runs']} wins, waves {s['avg_waves_cleared']}/3, freezing defeats {s['deaths_by_freezing']}, "
          f"fights {s['avg_real_seconds']} s real")
    print(f"    marksmen: {s['marksman_shots_per_run']} shots/run, hit {s['marksman_hit_rate']:.0%}, killed {s['marksman_kill_share']:.0%}; "
          f"bot assaults {s['bot_assaults_per_run']}/run")
    print(f"    pack: flank orders {s['flank_orders_per_run']}/run, fallbacks {s['fallbacks_per_run']}/run, backstabs {s['backstabs_per_run']}/run")
    answers, error = jev_client.ask({"fights": facts["words"]}, {
        k: v for k, v in coach.build_questions().items() if k in ("fairness", "engagement", "enemy_ai_issue", "bot_weakness")})
    if error:
        print(f"    Jev unavailable: {error}")
        return 0
    issue, conf = jev_client.choice(answers, "enemy_ai_issue")
    bot, bot_conf = jev_client.choice(answers, "bot_weakness")
    print(f"    Jev: fairness {jev_client.score(answers, 'fairness'):.2f}/4, engagement {jev_client.score(answers, 'engagement'):.2f}/4; "
          f"enemy AI: {issue} ({conf:.2f}); bot: {bot} ({bot_conf:.2f})")
    return 0


def cmd_log(path, question, pattern):
    lines = read(path)
    if pattern:
        lines = [l for l in lines if re.search(pattern, l)]
    else:
        lines = [l for l in lines if "LogCodexTactics" in l or (ERROR_RE.search(l) and not NOISE_RE.search(l))]
    evidence = [l.strip()[-200:] for l in lines][-40:]
    answers, error = jev_client.ask({"log": evidence}, {"q": {"type": "noul", "instructions": question + " (judge only by `log`)"}})
    value = jev_client.noul(answers, "q")
    print(f"{os.path.basename(path)}: {len(lines)} relevant lines; Jev yes-probability: "
          + (f"{value:.2f}" if value is not None else f"n/a ({error})"))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    smoke = sub.add_parser("smoke")
    smoke.add_argument("names", nargs="+")
    tests = sub.add_parser("tests")
    tests.add_argument("path", nargs="?", default=os.path.join(LOGS, "AutomationTests.log"))
    bot = sub.add_parser("bot")
    bot.add_argument("--profile", default="VETERAN")
    bot.add_argument("--runs", type=int, default=8)
    log = sub.add_parser("log")
    log.add_argument("path")
    log.add_argument("--ask", required=True)
    log.add_argument("--grep")
    args = parser.parse_args()
    if args.command == "smoke":
        sys.exit(cmd_smoke(args.names))
    if args.command == "tests":
        sys.exit(cmd_tests(args.path))
    if args.command == "bot":
        sys.exit(cmd_bot(args.profile.upper(), args.runs))
    sys.exit(cmd_log(args.path, args.ask, args.grep))


if __name__ == "__main__":
    main()
