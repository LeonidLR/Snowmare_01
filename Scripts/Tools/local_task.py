"""Run a small task on the LOCAL model (Ollama on the designer's RTX 3080 Laptop, 16 GB) - free, no tokens of any subscription.

For level-1 / small level-2 work only (model router): JSON edits, one-file explanations, regex / git / PowerShell one-liners,
test boilerplate after a neighbour, commit messages, short summaries. NOT for multi-file C++, anim BPs or design.
Always verify the answer (tests / a quick read) before using it.

  python Scripts/Tools/local_task.py --prompt "..." [--files a.cpp b.json] [--out answer.md] [--model qwen2.5-coder:14b]
  python Scripts/Tools/local_task.py --prompt-file brief.md --files ... --out answer.md

Needs Ollama running (it starts with Windows; else run `ollama serve`). The first call after a while loads the model (~10-60 s,
longer while the night trainer uses the GPU); then ~30 tokens/s.
"""
import argparse
import json
import os
import sys
import time
import urllib.request

DEFAULT_MODEL = "qwen3-coder:30b"
URL = "http://127.0.0.1:11434/api/generate"
MAX_FILE_CHARS = 40000


def build_prompt(task: str, files: list) -> str:
    parts = [task.strip(), ""]
    for path in files or []:
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        if len(text) > MAX_FILE_CHARS:
            text = text[:MAX_FILE_CHARS] + "\n... [truncated]"
        parts += [f"--- FILE {path} ---", text, f"--- END {path} ---", ""]
    return "\n".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prompt")
    parser.add_argument("--prompt-file")
    parser.add_argument("--files", nargs="*", default=[])
    parser.add_argument("--out")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--ctx", type=int, default=16384, help="context window in tokens (VRAM grows with it)")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    task = args.prompt or ""
    if args.prompt_file:
        with open(args.prompt_file, encoding="utf-8") as handle:
            task = handle.read()
    if not task.strip():
        parser.error("give --prompt or --prompt-file")

    body = json.dumps({
        "model": args.model,
        "prompt": build_prompt(task, args.files),
        "stream": False,
        "options": {"num_ctx": args.ctx, "temperature": 0.2},
    }).encode("utf-8")
    request = urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json"})
    started = time.time()
    try:
        with urllib.request.urlopen(request, timeout=args.timeout) as response:
            data = json.loads(response.read().decode("utf-8"))
    except OSError as error:
        print(f"local model not reachable ({error}); start Ollama or run `ollama serve`", file=sys.stderr)
        return 2

    answer = data.get("response", "")
    speed = data.get("eval_count", 0) / max(1e-9, data.get("eval_duration", 1) / 1e9)
    stats = f"[local {args.model}] {time.time() - started:.1f} s, {data.get('prompt_eval_count', 0)} in / {data.get('eval_count', 0)} out, {speed:.1f} tok/s"
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(answer)
        print(stats + f" -> {args.out}")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(answer)
        print(stats, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
