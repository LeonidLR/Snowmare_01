"""Visual QA: asks Claude to look at in-engine frames / comparison sheets and report visual defects.

User decision 2026-10-21: use the Console API credits for automated visual checks (what the numeric smoke
asserts cannot see: T-pose, floating / sinking, missing weapon, wrong colours / magenta, holes, clipping).
Calibration first (`--manifest Scripts/Tools/visual_qa_calibration.json`): images whose answer we know.

Key: ANTHROPIC_API_KEY from the environment, else the user's Windows environment (HKCU\\Environment) -
never printed. Each manifest entry: {"image", "context", "expect": "pass" | "fail", "known_issues": [...]}.

    python Scripts/Tools/visual_qa.py --manifest Scripts/Tools/visual_qa_calibration.json [--model claude-sonnet-5-5]
    python Scripts/Tools/visual_qa.py --image path.png --context "what the frame should show"

Writes Saved/VisualQA/<name>_<timestamp>.json (+ .md summary) and prints the token cost.
"""
import argparse
import base64
import datetime
import io
import json
import os
import pathlib
import sys

import anthropic
from PIL import Image

REPO = pathlib.Path(__file__).resolve().parents[2]
OUT_DIR = REPO / "Saved" / "VisualQA"
MAX_EDGE = 1568  # longer edges are downscaled by the API anyway; do it here to save upload + tokens

# USD per million tokens (claude-api skill table, cached 2026-10-06): input, output, cache read.
PRICES = {
    "claude-haiku-5-5": (0.10, 0.50, 0.01),
    "claude-sonnet-5-5": (2.00, 10.00, 0.20),
    "claude-opus-5-5": (4.00, 20.00, 0.20),
}

SYSTEM = """You are the visual QA reviewer for CodexTactics, a top-down tactical survival game made in Unreal Engine 5.
You look at frames captured in the engine by automated tests and report visual defects a player would notice.

Frames often contain the game HUD (objective box, squad list, message log, minimap); ignore the HUD unless the
context asks about it. The HUD panels are SEMI-TRANSPARENT DARK rectangles: anything behind them looks darker and
less saturated. Never judge colour / brightness on a part of a body that is behind a HUD panel - compare only the
parts that are in the clear; if the parts that matter are covered, that cell is "cannot judge", not a defect.
Labels above characters ("Military Zombie", "Frost Hound", health numbers, "CRITICAL HIT", "KNOCKED DOWN") are normal.

Check every character the context names, one by one, for each required attachment it lists (e.g. a rifle in the
hands): look at both hands of each body explicitly before deciding - a missing weapon is a major defect even when
everything else looks right.

Comparison sheets: each cell shows the same character twice in the same pose - the OPTIMIZED body on the LEFT and
the ORIGINAL body on the RIGHT, unless the context says otherwise. The optimized body must look like the original:
same silhouette, colours, brightness, materials, attachments (weapons, backpacks, hair), same pose. Small sharpness
differences and slightly different lighting position are fine. A semi-transparent / faded original is a test
staging artefact, not a defect.

Defect types to look for:
- missing_part: a weapon, limb, hair, prop or body part that should be there is missing
- floating: feet / body clearly above the ground when the pose should touch it
- sinking: body clearly below the ground surface
- wrong_color: colour, brightness or saturation clearly differs from the original or from what the context expects
- texture_artifact: magenta / checkerboard / black or white patches, stretched or garbled texture
- hole_or_clipping: holes in the mesh, see-through parts, body intersecting itself or walls
- bad_pose: T-pose, A-pose, twisted or broken joints, stretched limbs
- other: anything else clearly wrong

Be calibrated: report only what you can actually see. If a frame is too small or occluded to judge, say so in
notes and use verdict "uncertain" rather than guessing. A pair you cannot compare (overlapping bodies, HUD over
the original, different poses) is not a defect: mention it in notes only. Verdict "fail" if any issue has
severity "major"; "uncertain" if nothing is wrong but important cells could not be judged; otherwise "pass".""""""

SCHEMA = {
    "type": "object",
    "properties": {
        "verdict": {"type": "string", "enum": ["pass", "fail", "uncertain"]},
        "issues": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "type": {"type": "string", "enum": ["missing_part", "floating", "sinking", "wrong_color", "texture_artifact",
                                                       "hole_or_clipping", "bad_pose", "other"]},
                    "where": {"type": "string", "description": "which frame / cell / side, e.g. 'row 2 left, optimized'"},
                    "severity": {"type": "string", "enum": ["minor", "major"]},
                    "description": {"type": "string"},
                },
                "required": ["type", "where", "severity", "description"],
                "additionalProperties": False,
            },
        },
        "notes": {"type": "string"},
    },
    "required": ["verdict", "issues", "notes"],
    "additionalProperties": False,
}


def load_api_key():
    key = os.environ.get("ANTHROPIC_API_KEY")
    if key:
        return key
    if sys.platform == "win32":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as reg:
                return winreg.QueryValueEx(reg, "ANTHROPIC_API_KEY")[0]
        except OSError:
            pass
    sys.exit("ANTHROPIC_API_KEY is not set (environment or HKCU\\Environment)")


def encode_image(path):
    image = Image.open(path).convert("RGB")
    if max(image.size) > MAX_EDGE:
        image.thumbnail((MAX_EDGE, MAX_EDGE))
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return base64.standard_b64encode(buffer.getvalue()).decode("ascii"), image.size


def review(client, model, effort, image_path, context):
    data, size = encode_image(image_path)
    response = client.messages.create(
        model=model,
        max_tokens=4000,
        system=[{"type": "text", "text": SYSTEM, "cache_control": {"type": "ephemeral"}}],
        output_config={"effort": effort, "format": {"type": "json_schema", "schema": SCHEMA}},
        messages=[{
            "role": "user",
            "content": [
                {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": data}},
                {"type": "text", "text": f"Context: {context}\nReview this image and report the verdict."},
            ],
        }],
    )
    usage = response.usage
    tokens = {
        "input": usage.input_tokens,
        "output": usage.output_tokens,
        "cache_read": getattr(usage, "cache_read_input_tokens", 0) or 0,
        "cache_write": getattr(usage, "cache_creation_input_tokens", 0) or 0,
    }
    if response.stop_reason == "refusal":
        return {"verdict": "uncertain", "issues": [], "notes": "model refused"}, tokens, size
    text = next((b.text for b in response.content if b.type == "text"), "")
    try:
        return json.loads(text), tokens, size
    except json.JSONDecodeError:
        return {"verdict": "uncertain", "issues": [], "notes": f"unparsable reply (stop_reason {response.stop_reason})"}, tokens, size


def cost_usd(model, tokens):
    price_in, price_out, price_cache = PRICES.get(model, PRICES["claude-opus-5-5"])
    return (tokens["input"] * price_in + tokens["cache_write"] * price_in * 1.25 + tokens["cache_read"] * price_cache
            + tokens["output"] * price_out) / 1e6


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest")
    parser.add_argument("--image")
    parser.add_argument("--context", default="")
    parser.add_argument("--model", default="claude-sonnet-5-5")
    parser.add_argument("--effort", default="medium", choices=["low", "medium", "high", "xhigh", "max"])
    args = parser.parse_args()

    if args.manifest:
        entries = json.loads((REPO / args.manifest).read_text(encoding="utf-8"))["entries"]
        name = pathlib.Path(args.manifest).stem
    elif args.image:
        entries = [{"image": args.image, "context": args.context}]
        name = "single"
    else:
        parser.error("--manifest or --image")

    client = anthropic.Anthropic(api_key=load_api_key())
    results, total = [], 0.0
    for entry in entries:
        path = REPO / entry["image"]
        result, tokens, size = review(client, args.model, args.effort, path, entry["context"])
        spent = cost_usd(args.model, tokens)
        total += spent
        expect = entry.get("expect")
        match = None if expect is None else (result["verdict"] == expect)
        results.append({**entry, "result": result, "tokens": tokens, "cost_usd": round(spent, 5), "image_size": size, "match": match})
        flag = "" if match is None else ("OK  " if match else "MISS")
        print(f"{flag} {result['verdict']:9} expect={expect or '-':5} ${spent:.4f}  {entry['image']}")
        for issue in result["issues"]:
            print(f"        - [{issue['severity']}] {issue['type']} @ {issue['where']}: {issue['description']}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    report = OUT_DIR / f"{name}_{args.model}_{stamp}.json"
    report.write_text(json.dumps({"model": args.model, "effort": args.effort, "total_usd": round(total, 4), "results": results},
                                 indent=2, ensure_ascii=False), encoding="utf-8")
    checked = [r for r in results if r["match"] is not None]
    if checked:
        print(f"calibration: {sum(r['match'] for r in checked)}/{len(checked)} verdicts as expected")
    print(f"total ${total:.4f} ({args.model}, effort {args.effort}) -> {report.relative_to(REPO)}")


if __name__ == "__main__":
    main()
