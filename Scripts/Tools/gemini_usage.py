"""Token / cost usage of one Gemini CLI run (2026-11-16) - used by gemini_night.ps1 for the per-task chat report.

    python Scripts/Tools/gemini_usage.py <run log> --task 071 [--model gemini-3.8-flash]

Reads the JSON that `gemini -o json` prints at the end of the run (stats.models.<model>.tokens), adds the run to the night ledger
Saved/Orchestrator/gemini_budget.json and prints ONE line: "tokens in X / out Y / total Z; cost $C (night total: N tokens, $T)".
Prices per 1M tokens come from Scripts/gemini_prices.json (fill them in; unknown price -> "cost n/a"). Exit 0 always.
"""
import argparse
import datetime
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LEDGER = os.path.join(REPO, "Saved", "Orchestrator", "gemini_budget.json")
PRICES = os.path.join(REPO, "Scripts", "gemini_prices.json")


def last_stats(text):
    """The last JSON object in the log that has a "stats" key."""
    best = None
    start = text.find("{")
    while start != -1:
        depth, i, in_str, esc = 0, start, False, False
        while i < len(text):
            c = text[i]
            if in_str:
                esc = (c == "\\" and not esc)
                if c == '"' and not esc:
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    try:
                        obj = json.loads(text[start:i + 1])
                        if isinstance(obj, dict) and "stats" in obj:
                            best = obj
                    except ValueError:
                        pass
                    break
            i += 1
        start = text.find("{", start + 1)
    return best


def tokens_of(stats):
    """(fresh input, cached input, output incl. thoughts, requests). "prompt" = every request's whole context; "cached" of it is
    billed at the cache price, "input" is the fresh part."""
    fresh = cached = out = req = 0
    for _name, m in (stats.get("models") or {}).items():
        t = m.get("tokens") or {}
        prompt = int(t.get("prompt") or 0)
        c = int(t.get("cached") or 0)
        cached += c
        fresh += int(t.get("input") if t.get("input") is not None else max(prompt - c, 0))
        out += int(t.get("candidates") or 0) + int(t.get("thoughts") or 0)
        req += int((m.get("api") or {}).get("totalRequests") or 0)
    return fresh, cached, out, req


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--task", default="?")
    ap.add_argument("--model", default="")
    a = ap.parse_args()
    try:
        text = open(a.log, encoding="utf-8", errors="replace").read()
    except OSError:
        print("tokens n/a (no log)")
        return 0
    obj = last_stats(text)
    if not obj:
        print("tokens n/a (no JSON stats in the log - run ended early?)")
        return 0
    fresh, cached, tout, req = tokens_of(obj["stats"])
    tin, total = fresh, fresh + cached + tout
    prices = {}
    if os.path.exists(PRICES):
        try:
            prices = json.load(open(PRICES, encoding="utf-8"))
        except ValueError:
            prices = {}
    p = prices.get(a.model) or {}
    cost = None
    if p.get("input_per_1m") is not None and p.get("output_per_1m") is not None:
        cost = (fresh / 1e6 * float(p["input_per_1m"]) + tout / 1e6 * float(p["output_per_1m"])
                + cached / 1e6 * float(p.get("cached_per_1m") if p.get("cached_per_1m") is not None else p["input_per_1m"]))
    ledger = {"runs": []}
    if os.path.exists(LEDGER):
        try:
            ledger = json.load(open(LEDGER, encoding="utf-8"))
        except ValueError:
            pass
    ledger.setdefault("runs", []).append({"task": a.task, "model": a.model, "in": fresh, "cached": cached, "out": tout, "requests": req, "total": total,
                                          "cost": cost, "at": datetime.datetime.now().isoformat(timespec="seconds")})
    os.makedirs(os.path.dirname(LEDGER), exist_ok=True)
    json.dump(ledger, open(LEDGER, "w", encoding="utf-8"), indent=1)
    today = datetime.date.today().isoformat()
    day = [r for r in ledger["runs"] if str(r.get("at", "")).startswith(today)]
    day_tokens = sum(int(r.get("total") or 0) for r in day)
    day_cost = sum(float(r["cost"]) for r in day if r.get("cost") is not None)
    c = "cost n/a (set the price in Scripts/gemini_prices.json)" if cost is None else "cost ${:.2f}".format(cost)
    print("tokens: fresh in {:,} + cached {:,} + out {:,} ({} requests); {} (today: {:,} tokens, ${:.2f})".format(
        fresh, cached, tout, req, c, day_tokens, day_cost))
    return 0


if __name__ == "__main__":
    sys.exit(main())
