"""Single-shot Claude API job with a hard budget (the user's API credit, 2026-11-07).

For read-only work that doesn't need an agent loop: reviews of a diff, diagnoses from given files, design-doc drafts,
telemetry / report analysis. The prompt + the given files go in, one answer comes out (written to --out).
Spend is logged to Saved/Orchestrator/api_spend.jsonl; a job is refused when the logged total would pass --budget-usd.

    python Scripts/Tools/api_task.py --model claude-sonnet-5-5 --prompt-file p.md --files a.cpp b.md --out review.md
Needs ANTHROPIC_API_KEY in the environment (never printed).
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import sys

# USD per million tokens (input, output) - keep in sync with the current price list.
PRICES = {
    "claude-haiku-5-5": (1.0, 5.0),
    "claude-sonnet-5-5": (3.0, 15.0),
    "claude-opus-5-5": (15.0, 75.0),
}
LEDGER = os.path.join("Saved", "Orchestrator", "api_spend.jsonl")


def spent_so_far() -> float:
    if not os.path.exists(LEDGER):
        return 0.0
    total = 0.0
    with open(LEDGER, encoding="utf-8") as f:
        for line in f:
            try:
                total += float(json.loads(line).get("usd", 0.0))
            except (ValueError, json.JSONDecodeError):
                pass
    return total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="claude-sonnet-5-5", choices=sorted(PRICES))
    ap.add_argument("--prompt-file", required=True)
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-tokens", type=int, default=8000)
    ap.add_argument("--budget-usd", type=float, default=100.0, help="total cap over the whole ledger")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    if not os.environ.get("ANTHROPIC_API_KEY"):
        print("ANTHROPIC_API_KEY is not set", file=sys.stderr)
        return 2
    prompt = open(args.prompt_file, encoding="utf-8").read()
    parts = [prompt]
    for path in args.files:
        with open(path, encoding="utf-8", errors="replace") as f:
            parts.append(f"\n\n===== FILE: {path} =====\n{f.read()}")
    text = "".join(parts)

    in_price, out_price = PRICES[args.model]
    worst = len(text) / 3.5 / 1e6 * in_price + args.max_tokens / 1e6 * out_price
    already = spent_so_far()
    if already + worst > args.budget_usd:
        print(f"refused: spent {already:.2f} + worst case {worst:.2f} > budget {args.budget_usd:.2f} USD", file=sys.stderr)
        return 3

    import anthropic

    client = anthropic.Anthropic()
    # Streamed (2026-11-10): the SDK refuses a non-streamed call whose max_tokens may run past 10 minutes (Opus, 24k).
    with client.messages.stream(model=args.model, max_tokens=args.max_tokens,
                                messages=[{"role": "user", "content": text}]) as stream:
        msg = stream.get_final_message()
    answer = "".join(block.text for block in msg.content if getattr(block, "type", "") == "text")
    if not answer.strip():
        # 2026-11-10: a big input can spend the whole max_tokens before any text (stop_reason max_tokens) - say so, don't write silence.
        print(f"warning: no text in the answer (stop_reason {msg.stop_reason}) - raise --max-tokens", file=sys.stderr)
    usd = msg.usage.input_tokens / 1e6 * in_price + msg.usage.output_tokens / 1e6 * out_price
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(answer)
    os.makedirs(os.path.dirname(LEDGER), exist_ok=True)
    with open(LEDGER, "a", encoding="utf-8") as f:
        f.write(json.dumps({"when": dt.datetime.now().isoformat(timespec="seconds"), "model": args.model,
                            "label": args.label, "in": msg.usage.input_tokens, "out": msg.usage.output_tokens,
                            "usd": round(usd, 4)}) + "\n")
    print(f"ok: {args.out}  ({msg.usage.input_tokens} in / {msg.usage.output_tokens} out, {usd:.3f} USD; "
          f"total {already + usd:.2f} / {args.budget_usd:.2f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
